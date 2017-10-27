/*
* Copyright (C)2017  iCub Facility - Istituto Italiano di Tecnologia
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

#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <yarp/os/Network.h>
#include <yarp/os/RFModule.h>
#include <yarp/os/Bottle.h>
#include <yarp/os/BufferedPort.h>
#include <yarp/os/ResourceFinder.h>
#include <yarp/os/Subscriber.h>
#include <yarp/os/Node.h>
#include <yarp/os/Os.h>
#include <yarp/os/Time.h>
#include <yarp/os/Stamp.h>
#include <yarp/sig/Vector.h>
#include <yarp/dev/Drivers.h>
#include <yarp/dev/PolyDriver.h>
#include <yarp/os/RateThread.h>
#include <yarp/dev/ControlBoardInterfaces.h>
#include <yarp/dev/IFrameTransform.h>
#include <yarp/math/Math.h>
#include <iCub/ctrl/pids.h>
#include <string>
#include <math.h>

using namespace std;
using namespace yarp::os;
using namespace yarp::dev;

class Controller
{
private:
    yarp::os::Stamp           m_timeStamp;
    PolyDriver                m_ptf;
    IFrameTransform*          m_iTf;
    double                    m_current_theta;
    double                    m_current_x;
    double                    m_current_y;
    BufferedPort<Bottle>      m_port_odometry;
    BufferedPort<Bottle>      m_port_odometer;
    bool                      m_is_holonomic;

    //variables to control odometry simulated errors
    double              odometry_error_x_gain;
    double              odometry_error_y_gain;
    double              odometry_error_t_gain;
public:
    Controller();
    ~Controller();
    void   apply_control(double& lin_spd , double& ang_spd, double& des_dir, double& pwm_gain);
    void   get_odometry(double& x, double& y, double& theta);
    void   reset(double x = 0, double y = 0, double t = 0);
    void   publish_tf();
    void   publish_port();
    bool   init(bool holonomic);
    void   set_odometry_error (double xgain, double ygain, double tgain);


};

#endif
