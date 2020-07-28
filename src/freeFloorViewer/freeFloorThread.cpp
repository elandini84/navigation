﻿/* 
 * Copyright (C)2011  Department of Robotics Brain and Cognitive Sciences - Istituto Italiano di Tecnologia
 * Author: Marco Randazzo
 * email:  marco.randazzo@iit.it
 * website: www.robotcub.org
 * Permission is granted to copy, distribute, and/or modify this program
 * under the terms of the GNU General Public License, version 2 or any
 * later version published by the Free Software Foundation.
 *
 * A copy of the license can be found at
 * http://www.robotcub.org/icub/license/gpl.txt
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details
*/


#define _USE_MATH_DEFINES

#include "freeFloorThread.h"

YARP_LOG_COMPONENT(FREE_FLOOR_THREAD, "navigation.freeFloorViewer.freeFloorThread")
bool print{true};

void rotateAndCheck(yarp::sig::PointCloud<yarp::sig::DataXYZ>& inputPc,
                    const yarp::sig::Matrix& m,
                    const yarp::sig::FlexImage& inputCanvas,
                    yarp::sig::ImageOf<yarp::sig::PixelBgra> &output,
                    std::vector<std::pair<size_t,size_t>>& goodPixels,
                    double threshold_low,double threshold_high)
{
    output.resize(inputPc.width(),inputPc.height());
    yarp::sig::PixelBgra pOk(0,255,0,0.6);
    yarp::sig::PixelBgra pKo(0,0,255,1.0);
    std::map<std::pair<int,int>,bool> columns;
    int scaler = 10;

    output.copy(inputCanvas);

    goodPixels.clear();
    for (size_t r=0; r<inputPc.height(); r++)
    {
        for(size_t c=0; c<inputPc.width();c++){
            auto v1 = inputPc(c,r).toVector4();
            auto v2 = m*v1;
            inputPc(c,r).x=v2(0);
            inputPc(c,r).y=v2(1);
            inputPc(c,r).z=v2(2);
            int xC = (int)(v2(0)*scaler);
            int yC = (int)(v2(1)*scaler);
            std::pair<int,int> tempKey(xC,yC);
            if(columns.count(tempKey)==0){
                columns[tempKey] = (v2(2)>=threshold_low && v2(2)<=threshold_high) || v2(2)<0;
            }
            if(v2(2)<threshold_low && v2(2)>=0 && !columns[tempKey]){
                std::pair<size_t,size_t> tempPair;
                tempPair.first = c;
                tempPair.second = r;
                goodPixels.push_back(tempPair);
                output.pixel(c,r) = pOk;
            }
        }
    }
}


FreeFloorThread::FreeFloorThread(double _period, yarp::os::ResourceFinder &rf):
    PeriodicThread(_period),
    TypedReaderCallback(),
    m_rf(rf)
{
    m_depth_width = 0;
    m_depth_height = 0;
    m_pc_stepx = 1;
    m_pc_stepy = 1;
    m_floor_height = 0.1;
    m_ceiling_height = 3.0;
    m_ground_frame_id = "/ground_frame";
    m_camera_frame_id = "/depth_camera_frame";
    m_imgOutPortName = "/freeFloorViewer/floorEnhanced:o";
    m_targetOutPortName = "/free_floor_viewer/target:o";
}

bool FreeFloorThread::threadInit()
{
#ifdef FREEFLOOR_DEBUG
    yCDebug(FREE_FLOOR_THREAD, "thread initialising...\n");
#endif

    // --------- Generic config --------- //
    if(m_rf.check("target_pos_port")) {m_targetOutPortName = m_rf.find("target_pos_port").asString();}
    if(m_rf.check("img_out_port")) {m_imgOutPortName = m_rf.find("img_out_port").asString();}

    // --------- Z related props -------- //
    bool okZClipRf = m_rf.check("Z_CLIPPING_PLANES");
    if(okZClipRf){
        yarp::os::Searchable& pointcloud_clip_config = m_rf.findGroup("Z_CLIPPING_PLANES");
        if (pointcloud_clip_config.check("floor_height"))   {m_floor_height = pointcloud_clip_config.find("floor_height").asFloat64();}
        if (pointcloud_clip_config.check("ceiling_height"))   {m_ceiling_height = pointcloud_clip_config.find("ceiling_height").asFloat64();}
        if (pointcloud_clip_config.check("ground_frame_id")) {m_ground_frame_id = pointcloud_clip_config.find("ground_frame_id").asString();}
        if (pointcloud_clip_config.check("camera_frame_id")) {m_camera_frame_id = pointcloud_clip_config.find("camera_frame_id").asString();}
    }

    // --------- RGBDSensor config --------- //
    bool okRgbdRf = m_rf.check("RGBD_SENSOR_CLIENT");
    if(!okRgbdRf){
        yCError(FREE_FLOOR_THREAD,"RGBD_SENSOR_CLIENT section missing in ini file");

        return false;
    }
    yarp::os::Property rgbdProp;
    rgbdProp.fromString(m_rf.findGroup("RGBD_SENSOR_CLIENT").toString());
    m_rgbdPoly.open(rgbdProp);
    if(!m_rgbdPoly.isValid()){
        yCError(FREE_FLOOR_THREAD,"Error opening PolyDriver check parameters");
        return false;
    }
    m_rgbdPoly.view(m_iRgbd);
    if(!m_iRgbd){
        yCError(FREE_FLOOR_THREAD,"Error opening iRGBD interface. Device not available");
        return false;
    }
    //Verify if this is needed
    yarp::os::Time::delay(0.1);

    // --------- TransformClient config --------- //
    bool okTransformRf = m_rf.check("TRANSFORM_CLIENT");
    if(!okTransformRf){
        yCError(FREE_FLOOR_THREAD,"TRANSFORM_CLIENT section missing in ini file");

        return false;
    }
    yarp::os::Property tcProp;
    tcProp.fromString(m_rf.findGroup("TRANSFORM_CLIENT").toString());
    m_tcPoly.open(tcProp);
    if(!m_tcPoly.isValid()){
        yCError(FREE_FLOOR_THREAD,"Error opening PolyDriver check parameters");
        return false;
    }
    m_tcPoly.view(m_iTc);
    if(!m_iTc){
        yCError(FREE_FLOOR_THREAD,"Error opening iFrameTransform interface. Device not available");
        return false;
    }
    //Verify if this is needed
    yarp::os::Time::delay(0.1);

    //get parameters data from the camera
    m_depth_width = m_iRgbd->getRgbWidth();
    m_depth_height = m_iRgbd->getRgbHeight();
    bool propintr  = m_iRgbd->getDepthIntrinsicParam(m_propIntrinsics);
    if(!propintr){
        return false;
    }
    yCInfo(FREE_FLOOR_THREAD) << "Depth Intrinsics:" << m_propIntrinsics.toString();
    m_intrinsics.fromProperty(m_propIntrinsics);

    m_imgOutPort.open(m_imgOutPortName);
    m_targetOutPort.open(m_targetOutPortName);

#ifdef FREEFLOOR_DEBUG
    yCDebug(FREE_FLOOR_THREAD, "... done!\n");
#endif

    return true;
}

void FreeFloorThread::run()
{
    //std::lock_guard<std::mutex> lock(m_floorMutex);
    bool depth_ok = m_iRgbd->getDepthImage(m_depth_image);
    if (depth_ok == false)
    {
        yCDebug(FREE_FLOOR_THREAD, "getDepthImage failed");
        return;
    }
    if (m_depth_image.getRawImage()==nullptr)
    {
        yCDebug(FREE_FLOOR_THREAD, "invalid image received");
        return;
    }

    bool rgb_ok = m_iRgbd->getRgbImage(m_rgbImage);
    if (rgb_ok == false)
    {
        yCDebug(FREE_FLOOR_THREAD, "getRgbImage failed");
        return;
    }
    if (m_rgbImage.getRawImage()==nullptr)
    {
        yCDebug(FREE_FLOOR_THREAD, "invalid image received");
        return;
    }

    if (m_depth_image.width()!=m_depth_width ||
        m_depth_image.height()!=m_depth_height)
    {
        yCDebug(FREE_FLOOR_THREAD,"invalid image size: (%d %d) vs (%d %d)",m_depth_image.width(),m_depth_image.height(),m_depth_width,m_depth_height);
        return;
    }

    //we compute the transformation matrix from the camera to the laser reference frame

    bool frame_exists = m_iTc->getTransform(m_camera_frame_id,m_ground_frame_id, m_transform_mtrx);
    if (frame_exists==false)
    {
        yCWarning(FREE_FLOOR_THREAD, "Unable to found m matrix");
    }

    //if (m_publish_ros_pc) {ros_compute_and_send_pc(pc,m_ground_frame_id);}//<-------------------------

    yarp::sig::ImageOf<yarp::sig::PixelBgra>& imgOut = m_imgOutPort.prepare();
    imgOut.zero();

    m_floorMutex.lock();
    //compute the point cloud
    m_pc = yarp::sig::utils::depthToPC(m_depth_image, m_intrinsics,m_pc_roi,m_pc_stepx,m_pc_stepy);
    rotateAndCheck(m_pc, m_transform_mtrx,m_rgbImage,imgOut,m_okPixels,m_floor_height,m_ceiling_height);
    m_floorMutex.unlock();

    m_imgOutPort.write();
}

void FreeFloorThread::onRead(yarp::os::Bottle &b){
    std::pair<size_t,size_t> gotPos;
    size_t u = b.get(0).asInt();
    if(u >= m_rgbImage.width() or u<0){
        yCError(FREE_FLOOR_THREAD, "Pixel outside image boundaries");
    }
    size_t v = b.get(1).asInt();
    if(v >= m_rgbImage.height() or v<0){
        yCError(FREE_FLOOR_THREAD, "Pixel outside image boundaries");
    }
    gotPos.first = (size_t)u;
    gotPos.second = (size_t)v;
    m_floorMutex.lock();
    bool pixelOk = std::find(m_okPixels.begin(), m_okPixels.end(), gotPos) != m_okPixels.end();

    if(pixelOk){
        yarp::os::Bottle& toSend = m_targetOutPort.prepare();
        toSend.clear();
        toSend.addFloat32(m_pc(u,v).x);
        toSend.addFloat32(m_pc(u,v).y);
        m_targetOutPort.write();
    }
    m_floorMutex.unlock();
}

void FreeFloorThread::threadRelease()
{
#ifdef FREEFLOOR_DEBUG
    yCDebug(FREE_FLOOR_THREAD, "Thread releasing...");
#endif

    if(m_rgbdPoly.isValid())
        m_rgbdPoly.close();

    if(m_tcPoly.isValid())
        m_tcPoly.close();
    if(!m_imgOutPort.isClosed()){
        m_imgOutPort.close();
    }
    if(!m_targetOutPort.isClosed()){
        m_targetOutPort.close();
    }

    yCInfo(FREE_FLOOR_THREAD, "Thread released");

#ifdef FREEFLOOR_DEBUG
    yCDebug(FREE_FLOOR_THREAD, "... done.");
#endif

    return;
}