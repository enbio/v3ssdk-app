/**
 * @file fbwrite_video.cpp
 * Displays OpenCV video on framebuffer.
 * Compile with
 * g++ -o fbwrite_video -lopencv_core -lopencv_highgui -lopencv_imgproc fbwrite_video.cpp
 *
 * Contains code from https://stackoverflow.com/questions/4722301/writing-to-frame-buffer
 */

#include <iostream>                    // for std::cerr
#include <opencv2/imgproc/imgproc.hpp> // for cv::cvtColor
#include <opencv2/highgui/highgui.hpp> // for cv::VideoCapture
#include <fstream>                     // for std::ofstream
//#include <boost/timer/timer.hpp> // for boost::timer::cpu_timer

// this is C :/
#include <stdint.h>    // for uint32_t
#include <sys/ioctl.h> // for ioctl
#include <linux/fb.h>  // for fb_
#include <fcntl.h>     // for O_RDWR

#include <zbar.h>

struct framebuffer_info
{
    uint32_t bits_per_pixel;
    uint32_t xres_virtual;
};
struct framebuffer_info get_framebuffer_info(const char *framebuffer_device_path)
{
    struct framebuffer_info info;
    struct fb_var_screeninfo screen_info;
    int fd = -1;
    fd = open(framebuffer_device_path, O_RDWR);
    if (fd >= 0)
    {
        if (!ioctl(fd, FBIOGET_VSCREENINFO, &screen_info))
        {
            info.xres_virtual = screen_info.xres_virtual;
            info.bits_per_pixel = screen_info.bits_per_pixel;
        }
    }
    return info;
};


using namespace std;
using namespace cv;
using namespace zbar;

//zbar接口
string ZbarDecoder(Mat img)
{
    string result;
    ImageScanner scanner;
    const void *raw = (&img)->data;
    // configure the reader
    scanner.set_config(ZBAR_QRCODE, ZBAR_CFG_ENABLE, 1);
    // wrap image data
    Image image(img.cols, img.rows, "Y800", raw, img.cols * img.rows);
    // scan the image for barcodes
    int n = scanner.scan(image);
    // extract results
    result = image.symbol_begin()->get_data();
    image.set_data(NULL, 0);
    return result;
}

//对二值图像进行识别，如果失败则开运算进行二次识别
string GetQRInBinImg(Mat binImg)
{
    string result = ZbarDecoder(binImg);
    if(result.empty())
    {
        Mat openImg;
        Mat element = getStructuringElement(MORPH_RECT, Size(3, 3));
        morphologyEx(binImg, openImg, MORPH_OPEN, element);
        result = ZbarDecoder(openImg);
    }
    return result;
}

//main function
string GetQR(Mat img)
{
    Mat binImg;
    //在otsu二值结果的基础上，不断增加阈值，用于识别模糊图像
    int thre = threshold(img, binImg, 0, 255, cv::THRESH_OTSU);
    string result;
    while(result.empty() && thre<255)
    {
        threshold(img, binImg, thre, 255, cv::THRESH_BINARY);
        result = GetQRInBinImg(binImg);
        thre += 20;//阈值步长设为20，步长越大，识别率越低，速度越快
    }
    return result;
}


int main(int, char **)
{
    const int frame_width = 320;
    const int frame_height = 240;
    const int frame_rate = 10;
    framebuffer_info fb_info = get_framebuffer_info("/dev/fb0");
    cv::VideoCapture cap(1);
    if (!cap.isOpened())
    {
        std::cerr << "Could not open video device." << std::endl;
        return 1;
    }
    else
    {
        std::cout << "Successfully opened video device." << std::endl;
        cap.set(CV_CAP_PROP_FRAME_WIDTH, frame_width);
        cap.set(CV_CAP_PROP_FRAME_HEIGHT, frame_height);
        cap.set(CV_CAP_PROP_FPS, frame_rate);
        std::ofstream ofs("/dev/fb0");
        cv::Mat frame;

        while (true)
        {
            string result;
            cap >> frame;
            if (frame.empty())
                continue;
            frame = frame(cv::Rect(160, 120, 320, 240));
            cv::transpose(frame, frame);
            cv::flip(frame, frame, 1);

            result = GetQR(frame);
            std::cout << result <<endl;

            if (frame.depth() != CV_8U)
            {
                std::cerr << "Not 8 bits per pixel and channel." << std::endl;
            }
            else if (frame.channels() != 3)
            {
                std::cerr << "Not 3 channels." << std::endl;
            }
            else
            {
                // 3 Channels (assumed BGR), 8 Bit per Pixel and Channel
                int framebuffer_width = fb_info.xres_virtual;
                int framebuffer_depth = fb_info.bits_per_pixel;
                cv::Size2f frame_size = frame.size();
                cv::Mat framebuffer_compat;
                switch (framebuffer_depth)
                {
                case 16:
                    cv::cvtColor(frame, framebuffer_compat, cv::COLOR_BGR2BGR565);
                    for (int y = 0; y < frame_size.height; y++)
                    {
                        ofs.seekp(y * framebuffer_width * 2);
                        ofs.write(reinterpret_cast<char *>(framebuffer_compat.ptr(y)), frame_size.width * 2);
                    }
                    break;
                case 32:
                {
                    std::vector<cv::Mat> split_bgr;
                    cv::split(frame, split_bgr);
                    split_bgr.push_back(cv::Mat(frame_size, CV_8UC1, cv::Scalar(255)));
                    cv::merge(split_bgr, framebuffer_compat);
                    for (int y = 0; y < frame_size.height; y++)
                    {
                        ofs.seekp(y * framebuffer_width * 4);
                        ofs.write(reinterpret_cast<char *>(framebuffer_compat.ptr(y)), frame_size.width * 4);
                    }
                }
                break;
                default:
                    std::cerr << "Unsupported depth of framebuffer." << std::endl;
                }
            }
        }
    }
}
