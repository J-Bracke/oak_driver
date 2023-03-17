
#include <ros/ros.h>

#include <iostream>
#include <cstdio>

#include <sensor_msgs/Image.h>
#include <sensor_msgs/CompressedImage.h>
#include <stereo_msgs/DisparityImage.h>
#include <camera_info_manager/camera_info_manager.h>

#include <dynamic_reconfigure/server.h>
#include "oak_driver/CameraControlConfig.h"

// Inludes common necessary includes for development using depthai library
#include <depthai/depthai.hpp>
#include <depthai_bridge/ImageConverter.hpp>
#include <depthai_bridge/ImuConverter.hpp>
#include <depthai_bridge/depthaiUtility.hpp>
#include <depthai_bridge/BridgePublisher.hpp>
#include <depthai_bridge/DisparityConverter.hpp>


//ToDo: Use member variables instead of global ones!
static std::shared_ptr<dai::DataInputQueue> configQueue = nullptr;
static std::shared_ptr<dai::DataInputQueue> controlQueue = nullptr;

std::tuple<dai::Pipeline, int, int>  createPipeline(const std::string& rgbResolution, const std::string& stereoResolution, const std::string& codec, const float fps = 30, const int quality = 90, bool disparity = true, bool rectify = true){
    dai::Pipeline pipeline;

    if (!disparity)
        rectify = false;

    // Sensors
    auto monoLeft = pipeline.create<dai::node::MonoCamera>();
    auto monoRight = pipeline.create<dai::node::MonoCamera>();
    auto colorCam = pipeline.create<dai::node::ColorCamera>();
    auto imu = pipeline.create<dai::node::IMU>();

    //auto stereo = pipeline.create<dai::node::StereoDepth>();

    // Encoding
    auto encLeft = pipeline.create<dai::node::VideoEncoder>();
    auto encRight = pipeline.create<dai::node::VideoEncoder>();
    auto encRGB = pipeline.create<dai::node::VideoEncoder>();
    
    // Output
    auto xoutLeft = pipeline.create<dai::node::XLinkOut>();
    auto xoutRight = pipeline.create<dai::node::XLinkOut>();
    auto xoutRGB = pipeline.create<dai::node::XLinkOut>();
    auto xoutImu = pipeline.create<dai::node::XLinkOut>();

    //auto xoutDisp = pipeline.create<dai::node::XLinkOut>();
    
    // Config and Control
    auto configIn = pipeline.create<dai::node::XLinkIn>();
    auto controlIn = pipeline.create<dai::node::XLinkIn>();


    // Stereo pair resolution
    float stereo_fps = fps;
    dai::node::MonoCamera::Properties::SensorResolution monoResolution;
    int stereoWidth, stereoHeight, rgbWidth, rgbHeight;
    if(stereoResolution == "720p") {
        monoResolution = dai::node::MonoCamera::Properties::SensorResolution::THE_720_P;
        stereoWidth = 1280;
        stereoHeight = 720;
    } else if(stereoResolution == "400p") {
        monoResolution = dai::node::MonoCamera::Properties::SensorResolution::THE_400_P;
        stereoWidth = 640;
        stereoHeight = 400;
    } else if(stereoResolution == "800p") {
        monoResolution = dai::node::MonoCamera::Properties::SensorResolution::THE_800_P;
        stereoWidth = 1280;
        stereoHeight = 800;
    } else if(stereoResolution == "480p") {
        monoResolution = dai::node::MonoCamera::Properties::SensorResolution::THE_480_P;
        stereoWidth = 640;
        stereoHeight = 480;
    } else {
        ROS_ERROR("Invalid parameter. -> monoResolution: %s", stereoResolution.c_str());
        throw std::runtime_error("Invalid mono camera resolution.");
    }

    // MonoCamera
    monoLeft->setResolution(monoResolution);
    monoLeft->setBoardSocket(dai::CameraBoardSocket::LEFT);
    monoLeft->setFps(stereo_fps);
    monoRight->setResolution(monoResolution);
    monoRight->setBoardSocket(dai::CameraBoardSocket::RIGHT);
    monoRight->setFps(stereo_fps);

    ROS_INFO("Mono Resolution: %i", static_cast<int>(monoResolution));

    // StereoDepth
    /*
    if (disparity) {
        stereo->setDefaultProfilePreset(dai::node::StereoDepth::PresetMode::HIGH_DENSITY);
        stereo->initialConfig.setConfidenceThreshold(200);        // Known to be best                           //ToDo: As Parameter!
        stereo->setRectifyEdgeFillColor(0);                              // black, to better see the cutout
        stereo->initialConfig.setLeftRightCheckThreshold(5);    // Known to be best                             //ToDo: As Parameter!
        stereo->setLeftRightCheck(true);                                                                     //ToDo: As Parameter!
        stereo->setExtendedDisparity(true);                                                                 //ToDo: As Parameter!
        stereo->setSubpixel(false);                                                                          //ToDo: As Parameter!
    }
    */

    // Configure Camera
    // Note: IMX378/214, needs 1080_P / 4_K / 12_MP. Defaulting to 1080_P
    dai::ColorCameraProperties::SensorResolution colorResolution;
    if(rgbResolution == "720p"){
        colorResolution = dai::ColorCameraProperties::SensorResolution::THE_720_P; 
    }else if(rgbResolution == "1080p" ){
        colorResolution = dai::ColorCameraProperties::SensorResolution::THE_1080_P; 
    }else if(rgbResolution == "800p" ){
        colorResolution = dai::ColorCameraProperties::SensorResolution::THE_800_P; 
    }else if(rgbResolution == "4K" ){
        colorResolution = dai::ColorCameraProperties::SensorResolution::THE_4_K; 
    }else if(rgbResolution == "12MP" ){
        colorResolution = dai::ColorCameraProperties::SensorResolution::THE_12_MP; 
    }else if(rgbResolution == "13MP" ){
        colorResolution = dai::ColorCameraProperties::SensorResolution::THE_13_MP; 
    }else{
        ROS_ERROR("Invalid parameter. -> Resolution: %s, Use Default: 1080p", rgbResolution.c_str());
        //throw std::runtime_error("Invalid color camera resolution.");
        colorResolution = dai::ColorCameraProperties::SensorResolution::THE_1080_P;
    }

    colorCam->setBoardSocket(dai::CameraBoardSocket::RGB);
    colorCam->setResolution(colorResolution);
    colorCam->setFps(fps);
    colorCam->setInterleaved(false);

    ROS_INFO("RGB Resolution: %i", static_cast<int>(colorResolution));
    ROS_INFO("FPS: %i", static_cast<int>(fps));


    // Imu
    imu->enableIMUSensor(dai::IMUSensor::ACCELEROMETER_RAW, 500);       //ToDo: As Parameter!
    imu->enableIMUSensor(dai::IMUSensor::GYROSCOPE_RAW, 400);           //ToDo: As Parameter!
    imu->setBatchReportThreshold(5);
    imu->setMaxBatchReports(20);  // Get one message only for now.

    
    //ToDo: Add support for no encoding /raw

    // Configure Codec
    dai::VideoEncoderProperties::Profile encoderProfile;
    if(codec == "H264_BASELINE"){
        encoderProfile = dai::VideoEncoderProperties::Profile::H264_BASELINE; 
    }else if(codec == "H264_HIGH" ){
        encoderProfile = dai::VideoEncoderProperties::Profile::H264_HIGH; 
    }else if(codec == "H264_MAIN" ){
        encoderProfile = dai::VideoEncoderProperties::Profile::H264_MAIN; 
    }else if(codec == "H265_MAIN" ){
        encoderProfile = dai::VideoEncoderProperties::Profile::H265_MAIN; 
    }else if(codec == "MJPEG" ){
        encoderProfile = dai::VideoEncoderProperties::Profile::MJPEG; 
    }else{
        ROS_ERROR("Invalid parameter. -> Codec: %s, Use Default: MJPEG", codec.c_str());
        //throw std::runtime_error("Invalid color camera codec.");
        encoderProfile = dai::VideoEncoderProperties::Profile::MJPEG; 
    }

    encRGB->setDefaultProfilePreset(fps, encoderProfile);
    encLeft->setDefaultProfilePreset(fps, encoderProfile);
    encRight->setDefaultProfilePreset(fps, encoderProfile);

    ROS_INFO("Codec: %i", static_cast<int>(encoderProfile));

    encRGB->setQuality(quality);
    encLeft->setQuality(quality);
    encRight->setQuality(quality);

    ROS_INFO("Quality: %i", quality);

    //encRGB->setFrameRate(fps);


    // Configure and Control Input
    configIn->setStreamName("config");
    configIn->out.link(colorCam->inputConfig);
    
    controlIn->setStreamName("control");
    controlIn->out.link(colorCam->inputControl);
    //ToDo: Camera control currently only for RGB currently!
    
/*
    stereo->setRectifyEdgeFillColor(0);
    monoLeft->out.link(stereo->left);
    monoRight->out.link(stereo->right);
    stereo->disparity.link(xoutDisp->input);
*/

    // Link plugins CAM -> Encoder-> XLINK
    monoLeft->out.link(encLeft->input);
    monoRight->out.link(encRight->input);

/*
    if(rectify) {
        stereo->rectifiedLeft.link(encLeft->input);
        stereo->rectifiedRight.link(encRight->input);
    } else {
        stereo->syncedLeft.link(encLeft->input);
        stereo->syncedRight.link(encRight->input);
    }
*/


    encLeft->bitstream.link(xoutLeft->input);
    encRight->bitstream.link(xoutRight->input);

    colorCam->video.link(encRGB->input);
    encRGB->bitstream.link(xoutRGB->input);

    
    // Link IMU to output
    imu->out.link(xoutImu->input);

    // Configure Output
    xoutLeft->setStreamName("left");
    xoutRight->setStreamName("right");
    xoutRGB->setStreamName("rgb");
    xoutImu->setStreamName("imu");

    //xoutDisp->setStreamName("disp");
    

    return std::make_tuple(pipeline, stereoWidth, stereoHeight);

}


void reconfigureCallback(oak_driver::CameraControlConfig &config, uint32_t level){
    auto ctrl = dai::CameraControl();
    //ctrl.setCaptureStill(config.CaptureStill);
    ctrl.setAutoFocusMode((dai::CameraControl::AutoFocusMode)config.auto_focus_mode);
    if (config.auto_focus_mode == 0)
    {
        // Auto Focus Off -> Manual Focus
        ctrl.setManualFocus(config.manual_focus);

    }
    else if (config.auto_focus_mode < 3 || config.auto_focus_mode == 5)
    {
        // Disable Continous
        ctrl.setAutoFocusTrigger();
    }
        
    
    if (config.manual_exposure_time == 0)
    {
        // Auto Exposure
        ctrl.setAutoExposureEnable();
        ctrl.setAutoExposureLock(config.auto_exposure_lock);
        ctrl.setAutoExposureCompensation(config.auto_exposure_compensation);
        ctrl.setAntiBandingMode((dai::CameraControl::AntiBandingMode)config.anti_banding_mode);
    }
    else
    {
        // Manual Exposure
        ctrl.setManualExposure(config.manual_exposure_time, config.manual_exposure_iso);
    }

    ctrl.setAutoWhiteBalanceMode((dai::CameraControl::AutoWhiteBalanceMode)config.auto_white_balance_mode);
    if (config.auto_white_balance_mode > 0)
    {
        // Auto White Balance
        ctrl.setAutoWhiteBalanceLock(config.auto_white_balance_mode);
    }
    else
    {
        // Manual White Balance
        ctrl.setManualWhiteBalance(config.manual_white_balance);
    }


    if(controlQueue)
        controlQueue->send(ctrl);

}


int main(int argc, char** argv){

    ros::init(argc, argv, "stereo_node");
    ros::NodeHandle pnh("~");
    
    std::string tfPrefix;
    std::string camera_param_uri;
    std::string device_name;
    std::string rgbResolution;
    std::string stereoResolution;
    std::string codec;

    float fps;
    int quality; //between 0 and 100
    bool disparity;
    bool rectify;   //only relevant for out_LR!

    int badParams = 0;

    badParams += !pnh.getParam("tf_prefix", tfPrefix);
    badParams += !pnh.getParam("camera_param_uri", camera_param_uri);

    if (badParams > 0) {
        throw std::runtime_error("Couldn't find one of the parameters");
    }

    // Optional Parameter:
    pnh.param<std::string>("device_name", device_name, "");
    pnh.param<std::string>("rgb_resolution", rgbResolution, "1080p");
    pnh.param<std::string>("stereo_resolution", stereoResolution, "720p");
    pnh.param<std::string>("codec", codec, "MJPEG");

    pnh.param<float>("fps", fps, 30);
    pnh.param<int>("quality", quality, 90);
    pnh.param<bool>("disparity", disparity, false);
    pnh.param<bool>("rectify", rectify, false);

    camera_info_manager::CameraInfoManager camInfoMang(pnh, tfPrefix, camera_param_uri);
    sensor_msgs::CameraInfo camInfo_ = camInfoMang.getCameraInfo();

    if (!disparity)
        rectify = false;

    // Create pipeline
    dai::Pipeline pipeline;
    int width, height;
    std::tie(pipeline, width, height) = createPipeline(rgbResolution, stereoResolution, codec, fps, quality, disparity, rectify);

    // Connect to device and start pipeline
    std::shared_ptr<dai::Device> device = nullptr;

    if (device_name.empty()) {
        ROS_INFO("Device Name: <empty>");
        device = std::make_shared<dai::Device>(pipeline); //First device that could be found.
    }
    else {
        ROS_INFO("Device Name: %s",  device_name.c_str());
        device = std::make_shared<dai::Device>(pipeline, dai::DeviceInfo(device_name));
    }

    configQueue = device->getInputQueue("config");
    controlQueue = device->getInputQueue("control");

    // Output queue will be used to get the encoded data from the output defined above
    auto rgbQueue = device->getOutputQueue("rgb", 30, true);
    
    auto imuQueue = device->getOutputQueue("imu", 30, false);
    //auto dispQueue = device->getOutputQueue("disp", 30, false);

    auto calibrationHandler = device->readCalibration();

    /*
    std::vector<std::tuple<std::string, int, int>> irDrivers = device->getIrDrivers();
    if(!irDrivers.empty()) {
        if(enableDotProjector) {
            device->setIrLaserDotProjectorBrightness(dotProjectormA);
        }

        if(enableFloodLight) {
            device->setIrFloodLightBrightness(floodLightmA);
        }
    }
    */

    dai::rosBridge::ImageConverter leftConverter(tfPrefix + "_left_camera_optical_frame", true);
    dai::rosBridge::ImageConverter rightConverter(tfPrefix + "_right_camera_optical_frame", true);

    std::string leftPubName = std::string("left/compressed");     // alias left/image_raw
    std::string rightPubName = std::string("right/compressed");  // alias right/image_raw
    if (disparity && rectify) {
        leftPubName = std::string("left/compressed_rect");     // alias left/image_rect
        rightPubName = std::string("right/compressed_rect");  // alias right/image_rect
    }

    // Dynamic Reconfigure Parameter
    dynamic_reconfigure::Server<oak_driver::CameraControlConfig> dynReconfServer;
    dynamic_reconfigure::Server<oak_driver::CameraControlConfig>::CallbackType dynReconfCB;

    dynReconfCB = boost::bind(&reconfigureCallback, _1, _2);
    dynReconfServer.setCallback(dynReconfCB);



    ROS_INFO("Start data stream!" );

    // IMU
    /*
    dai::rosBridge::ImuConverter imuConverter(tfPrefix + "_imu_frame", imuMode, linearAccelCovariance, angularVelCovariance);

    dai::rosBridge::BridgePublisher<sensor_msgs::Imu, dai::IMUData> imuPublish(
        imuQueue,
        pnh,
        std::string("imu"),
        std::bind(&dai::rosBridge::ImuConverter::toRosMsg, &imuConverter, std::placeholders::_1, std::placeholders::_2),
        30,
        "",
        "imu");
        

    imuPublish.addPublisherCallback();
    */

    // RGB
    dai::rosBridge::ImageConverter rgbConverter(tfPrefix + "_rgb_camera_optical_frame", false);
    auto rgbCameraInfo = rgbConverter.calibrationToCameraInfo(calibrationHandler, dai::CameraBoardSocket::RGB, width, height);
    auto imgQueue = device->getOutputQueue("rgb", 30, false);
    dai::rosBridge::BridgePublisher<sensor_msgs::CompressedImage, dai::ImgFrame> rgbPublish(
        imgQueue,
        pnh,
        std::string("color/compressed"),
        std::bind(&dai::rosBridge::ImageConverter::toComprRosMsg, &rgbConverter, std::placeholders::_1, std::placeholders::_2),
        30,
        rgbCameraInfo,
        "color");
    rgbPublish.addPublisherCallback();

    // Depth
    /*
    if (disparity)  // about 30MB/s at 400p@30Hz uncompressed
    {
        dai::rosBridge::DisparityConverter dispConverter(tfPrefix + "_right_camera_optical_frame", 880, 7.5, 20, 2000);  // TODO(sachin): undo hardcoding of baseline

        auto disparityCameraInfo = leftConverter.calibrationToCameraInfo(calibrationHandler, dai::CameraBoardSocket::RIGHT, width, height);
        auto depthconverter = rightConverter;
        dai::rosBridge::BridgePublisher<stereo_msgs::DisparityImage, dai::ImgFrame> dispPublish(
            dispQueue,
            pnh,
            std::string("stereo/disparity"),
            std::bind(&dai::rosBridge::DisparityConverter::toRosMsg, &dispConverter, std::placeholders::_1, std::placeholders::_2),
            30,
            disparityCameraInfo,
            "stereo");
        dispPublish.addPublisherCallback();
    }
    */


    // Mono Stereo Pair
    auto leftCameraInfo = leftConverter.calibrationToCameraInfo(calibrationHandler, dai::CameraBoardSocket::LEFT, width, height);
    auto rightCameraInfo = leftConverter.calibrationToCameraInfo(calibrationHandler, dai::CameraBoardSocket::RIGHT, width, height);

    auto leftQueue = device->getOutputQueue("left", 30, false);
    auto rightQueue = device->getOutputQueue("right", 30, false);
    dai::rosBridge::BridgePublisher<sensor_msgs::CompressedImage, dai::ImgFrame> leftPublish(
        leftQueue,
        pnh,
        leftPubName,
        std::bind(&dai::rosBridge::ImageConverter::toComprRosMsg, &leftConverter, std::placeholders::_1, std::placeholders::_2),
        30,
        leftCameraInfo,
        "left");
    dai::rosBridge::BridgePublisher<sensor_msgs::CompressedImage, dai::ImgFrame> rightPublish(
        rightQueue,
        pnh,
        rightPubName,
        std::bind(&dai::rosBridge::ImageConverter::toComprRosMsg, &rightConverter, std::placeholders::_1, std::placeholders::_2),
        30,
        rightCameraInfo,
        "right");
    rightPublish.addPublisherCallback();
    leftPublish.addPublisherCallback();


    ros::spin();


    return 0;
}

