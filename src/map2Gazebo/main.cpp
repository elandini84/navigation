/*
* Copyright (C) 2006-2018 Istituto Italiano di Tecnologia (IIT)
* All rights reserved.
* Author: Marco Randazzo
* email:  marco.randazzo@iit.it

* This software may be modified and distributed under the terms of the
* BSD-3-Clause license. See the accompanying LICENSE file for details.
*/

/**
 * \section map2Gazebo
 * This module converts a Yarp map into an heightmap which can be imported into gazebo simualtor.
 */

#include <yarp/os/Network.h>
#include <yarp/os/RFModule.h>
#include <yarp/os/Time.h>
#include <yarp/os/Port.h>
#include <yarp/dev/IMap2D.h>
#include <yarp/os/Property.h>
#include <yarp/dev/PolyDriver.h>
#include <yarp/os/LogStream.h>
#include <math.h>
#include <fstream>
#include <string>
#include <iostream>
#include <algorithm>
#include <yarp/sig/ImageFile.h>

using namespace std;
using namespace yarp::os;
using namespace yarp::dev;

class map2GazeboModule : public yarp::os::RFModule
{
protected:
    yarp::os::Port               rpcPort;
    yarp::dev::PolyDriver        m_pMap;
    yarp::dev::IMap2D*           m_iMap;
    yarp::dev::MapGrid2D         m_yarp_map;

    string sdf_file_string =  "< ? xml version = \"1.0\" ? > \n \
<sdf version = \"1.5\">  \n \
    <model name = \"heightmap\">  \n \
        <static> true </static>  \n \
        <link name = \"link\">  \n \
\
            <collision name = \"collision\">  \n \
                <geometry> \n \
                    <heightmap> \n \
                        <uri>file://media/materials/textures/heightmap_bowl.png</uri>  \n \
                        <size> SIZE_XX SIZE_YY SIZE_ZZ </size>  \n \
                        <pos> POS_XX POS_YY POS_ZZ </pos>  \n \
                    </heightmap>  \n \
                </geometry>  \n \
            </collision>  \n \
\
            <visual name = \"visual_abcedf\">  \n \
                <geometry> \n \
                    <heightmap> \n \
                        <use_terrain_paging> false </use_terrain_paging> \n \
                        <texture> \n \
                           <diffuse>file://media/materials/textures/dirt_diffusespecular.png</diffuse> \n \
                           <normal>file ://media/materials/textures/flat_normal.png</normal> \n \
                           <size> 1 </size> \n \
                        </texture> \n \
                        <blend> \n \
                           <min_height> 4 </min_height> \n \
                           <fade_dist> 5 </fade_dist> \n \
                        </blend> \n \
                        <uri>file://media/materials/textures/heightmap_bowl.png</uri> \n \
                        <size> SIZE_XX SIZE_YY SIZE_ZZ </size>  \n \
                        <pos> POS_XX POS_YY POS_ZZ </pos>  \n \
                    </heightmap> \n \
                </geometry> \n \
            </visual> \n \
\
        </link> \n \
    </model> \n \
</sdf> \n";

public:
    map2GazeboModule()
    {
        m_iMap = nullptr;
    }

    void replace(const string& find_str, string& sdf_str, double val)
    {
        size_t pos = sdf_file_string.find(find_str);
        while (pos != std::string::npos)
        {
            sdf_file_string.replace(pos, find_str.size(), std::to_string(val));
            pos = sdf_file_string.find(find_str);
        }
    }

    virtual bool configure(yarp::os::ResourceFinder &rf)
    {
        bool ret = rpcPort.open("/map2Gazebo/rpc");
        if (ret == false)
        {
            yError() << "Unable to open module ports";
            return false;
        }
        attach(rpcPort);
        //attachTerminal();

        if (rf.check("from_file"))
        {
            string map_name = rf.find("from_file").asString();
            if (m_yarp_map.loadFromFile(map_name) == false)
            {
                yError() << "Failed to open map: " << map_name;
                return false;
            }
        }
        else if (rf.check("from_server"))
        {
            string map_name = rf.find("from_server").asString();
            //open a client for the server
            Property map_options;
            map_options.put("device", "map2DClient");
            map_options.put("local", "/map2Gazebo"); //This is just a prefix. map2DClient will complete the port name.
            map_options.put("remote", "/mapServer");
            if (m_pMap.open(map_options) == false)
            {
                yError() << "Unable to open mapClient";
                return false;
            }
            m_pMap.view(m_iMap);
            if (m_iMap == 0)
            {
                yError() << "Unable to open map interface";
                return false;
            }

            //get the map from server
            yInfo() << "Asking for map '" << map_name << "'...";
            bool b = m_iMap->get_map(map_name, m_yarp_map);
            m_yarp_map.crop(-1, -1, -1, -1);
            if (b)
            {
                yInfo() << "'" << map_name << "' received";
            }
            else
            {
                yError() << "'" << map_name << "' not found";
                return false;
            }
        }
        else
        {
            yError() << "missing options";
            return false;
        }

        double x0=0, y0=0, t0 = 0, r=0;
        size_t w=0, h=0;
        m_yarp_map.getOrigin(x0, y0, t0);
        m_yarp_map.getResolution(r);
        m_yarp_map.getSize_in_cells(w,h);

        //heightmaps in gazebo must be square, with size n^2+1. 
        size_t map_size = std::max(w, h);
        map_size--;
        map_size |= map_size >> 1;
        map_size |= map_size >> 2;
        map_size |= map_size >> 4;
        map_size |= map_size >> 8;
        map_size |= map_size >> 16;
        map_size++;
        map_size++;

        //heightmap color code is the following: black=bottom, white=top
        yarp::dev::MapGrid2D::map_flags flag;
        yarp::dev::MapGrid2D::XYCell cell;
        yarp::sig::ImageOf<yarp::sig::PixelMono> heightmap;
        heightmap.setQuantum(1);
        heightmap.resize(map_size, map_size);
        for (cell.y = 0; cell.y < map_size; cell.y++)
            for (cell.x = 0; cell.x < map_size; cell.x++)
            {
                heightmap.safePixel(cell.x, cell.y) = 50;
            }
        for (cell.y=0; cell.y<h; cell.y++)
            for (cell.x=0; cell.x < w; cell.x++)
            {
                m_yarp_map.getMapFlag(cell, flag);
                size_t computed_x = 0;
                size_t computed_y = 0;
                int align = 0;
                switch (align)
                {
                     case 0:
                    //align top-left
                    computed_x = cell.x;
                    computed_y = cell.y;
                    break;

                    case 1:
                    default:
                    //align center
                    computed_x = cell.x + (map_size - w) / 2;
                    computed_y = cell.y + (map_size - h) / 2;
                    break;
                }
                switch (flag)
                {
                    case yarp::dev::MapGrid2D::map_flags::MAP_CELL_WALL:
                    {
                        heightmap.safePixel(computed_x, computed_y) = 255;
                    } break;
                    case yarp::dev::MapGrid2D::map_flags::MAP_CELL_UNKNOWN:
                    {
                        heightmap.safePixel(computed_x, computed_y) = 50;
                    } break;
                    case yarp::dev::MapGrid2D::map_flags::MAP_CELL_FREE:
                    default:
                    {
                        heightmap.safePixel(computed_x, computed_y) = 0;
                    } break;
                }
            }

        //save the heightmap to disk
        yarp::sig::file::write(heightmap, "heightmap.png",yarp::sig::file::FORMAT_PNG);
        yInfo() << "File " << "heightmap.png" << "saved.";

        //process the sdf template
        size_t pos = 0;
        
        replace("POS_XX", sdf_file_string, x0);
        replace("POS_YY", sdf_file_string, y0);
        replace("POS_ZZ", sdf_file_string, 0.0);
        replace("SIZE_XX", sdf_file_string, map_size*r);
        replace("SIZE_YY", sdf_file_string, map_size*r);
        replace("SIZE_ZZ", sdf_file_string, 3.0);

        //save the sdf to disk
        std::ofstream out("output.sdf");
        out << sdf_file_string;
        out.close();
        yInfo() << "File "<< "output.sdf" << "saved.";

        return true;
    }

    virtual bool interruptModule()
    {
        rpcPort.interrupt();
        return true;
    }

    virtual bool close()
    {
        rpcPort.interrupt();
        rpcPort.close();
        return true;
    }

    virtual double getPeriod()
    { 
        return 3.0; 
    }
    
    virtual bool updateModule()
    { 
        return true; 
    }

    virtual bool respond(const yarp::os::Bottle& command,yarp::os::Bottle& reply) 
    {
        reply.clear(); 
        if (command.get(0).isString())
        {
            if (command.get(0).asString()=="quit")
            {
                return false;
            }

            else if (command.get(0).asString()=="help")
            {
                reply.addVocab(Vocab::encode("many"));
                reply.addString("Available commands are:");
                reply.addString("<not yet implemented>");
            }
        }
        else
        {
            yError() << "Invalid command type";
            reply.addString("err");
        }
        return true;
    }
};

int main(int argc, char *argv[])
{
    yarp::os::Network yarp;
    if (!yarp.checkNetwork())
    {
        yError("check Yarp network.\n");
        return -1;
    }

    yarp::os::ResourceFinder rf;
    rf.setVerbose(true);
    rf.configure(argc,argv);
    std::string debug_rf = rf.toString();

    if (rf.check("help"))
    {
        yInfo() << "Options:";
        yInfo() << "--from_server <map_id>";
        yInfo() << "--from_file <file.map>";
        return 0;
    }

    map2GazeboModule theModule;

    return theModule.runModule(rf);
}

 
