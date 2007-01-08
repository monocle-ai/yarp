// -*- mode:C++; tab-width:4; c-basic-offset:4; indent-tabs-mode:nil -*-

/*
 * Copyright (C) 2006 Paul Fitzpatrick
 * CopyPolicy: Released under the terms of the GNU GPL v2.0.
 *
 */


#include <yarp/String.h>
#include <yarp/Logger.h>
#include <yarp/os/Property.h>
#include <yarp/os/Time.h>
#include <yarp/os/Network.h>
#include <yarp/os/Terminator.h>
#include <yarp/dev/PolyDriver.h>
#include <yarp/dev/ServiceInterfaces.h>

#include <yarp/dev/Drivers.h>

#include <ace/OS.h>
#include <ace/Vector_T.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>

using namespace yarp;
using namespace yarp::os;
using namespace yarp::dev;
using namespace std;

Drivers Drivers::instance;

class DriversHelper {
public:
    ACE_Vector<DriverCreator *> delegates;

    ~DriversHelper() {
        for (unsigned int i=0; i<delegates.size(); i++) {
            delete delegates[i];
        }
        delegates.clear();
    }

    ConstString toString() {
        yarp::String s;
        for (unsigned int i=0; i<delegates.size(); i++) {
            ConstString name = delegates[i]->getName();
            ConstString wrapper = delegates[i]->getWrapper();
            s += "Device \"";
            s += delegates[i]->getName().c_str();
            s += "\"";
            s += ",";
            s += " C++ class ";
            s += delegates[i]->getCode().c_str();
            s += ", ";
            if (wrapper=="") {
                s += "has no network wrapper";
            } else if (wrapper!=name) {
                s += "wrapped by \"";
                s += delegates[i]->getWrapper().c_str();
                s += "\"";
            } else {
                s += "is a network wrapper.";
            }
            s += "\n";
        }
        return ConstString(s.c_str());
    }

    void add(DriverCreator *creator) {
        if (creator!=NULL) {
            delegates.push_back(creator);
        }
    }

    DriverCreator *find(const char *name) {
        for (unsigned int i=0; i<delegates.size(); i++) {
            yarp::String s = delegates[i]->toString().c_str();
            if (s==name) {
                return delegates[i];
            }
        }
        return NULL;
    }
};

#define HELPER(x) (*(((DriversHelper*)(x))))

Drivers::Drivers() {
    implementation = new DriversHelper;
    YARP_ASSERT(implementation!=NULL);
    init();
}


Drivers::~Drivers() {
    if (implementation!=NULL) {
        delete &HELPER(implementation);
        implementation = NULL;
    }
}

yarp::os::ConstString Drivers::toString() {
    return HELPER(implementation).toString();
}

void Drivers::add(DriverCreator *creator) {
    HELPER(implementation).add(creator);
}


DriverCreator *Drivers::find(const char *name) {
    return HELPER(implementation).find(name);
}


DeviceDriver *Drivers::open(yarp::os::Searchable& prop) {
    yarp::os::Searchable *config = &prop;
    Property p;
    String str = prop.toString().c_str();
    Value *part;
    if (prop.check("device",part)) {
        str = part->toString().c_str();
    }
    Bottle bot(str.c_str());
    if (bot.size()>1) {
        // this wasn't a device name, but some codes -- rearrange
        p.fromString(str.c_str());
        str = p.find("device").asString().c_str();
        config = &p;
    }
    YARP_DEBUG(Logger::get(),String("Drivers::open starting for ") + str);

    DeviceDriver *driver = NULL;

    DriverCreator *creator = find(str.c_str());
    if (creator!=NULL) {
        Value *val;
        if (config->check("wrapped",val)&&(creator->getWrapper()!="")) {
            String wrapper = creator->getWrapper().c_str();
            DriverCreator *wrapCreator = find(wrapper.c_str());
            if (wrapCreator!=NULL) {
                p.fromString(config->toString());
                p.unput("wrapped");
                config = &p;
                if (wrapCreator!=creator) {
                    p.put("subdevice",str.c_str());
                    p.put("device",wrapper.c_str());
                    p.setMonitor(prop.getMonitor(),
                                 wrapper.c_str()); // pass on any monitoring
                    driver = wrapCreator->create();
                    creator = wrapCreator;
                } else {
                    // already wrapped
                    driver = creator->create();
                }
            }
        } else {
            driver = creator->create();
        }
    } else {
        printf("yarpdev: ***ERROR*** could not find device <%s>\n", str.c_str());
    }

    YARP_DEBUG(Logger::get(),String("Drivers::open started for ") + str);

    if (driver!=NULL) {
        //printf("yarpdev: parameters are %s\n", config->toString().c_str());
        YARP_DEBUG(Logger::get(),String("Drivers::open config for ") + str);
        bool ok = driver->open(*config);
        YARP_DEBUG(Logger::get(),String("Drivers::open configed for ") + str);
        if (!ok) {
            printf("yarpdev: ***ERROR*** driver <%s> was found but could not open\n", config->find("device").toString().c_str());
            //YARP_DEBUG(Logger::get(),String("Discarding ") + str);
            delete driver;
            //YARP_DEBUG(Logger::get(),String("Discarded ") + str);
            driver = NULL;
        } else {
            if (creator!=NULL) {
                ConstString name = creator->getName();
                ConstString wrapper = creator->getWrapper();
                ConstString code = creator->getCode();
                printf("yarpdev: created %s <%s>.  See C++ class %s for documentation.\n",
                       (name==wrapper)?"wrapper":"device",
                       name.c_str(), code.c_str());
            }
        }
        return driver;
    }
    
    return NULL;
}


// helper method for "yarpdev" body
static void toDox(PolyDriver& dd, ostream& os) {
    os << "===============================================================" 
       << endl;
    os << "== Options checked by device:" << endl;
    os << "== " << endl;

    Bottle order = dd.getOptions();
    for (int i=0; i<order.size(); i++) {
        String name = order.get(i).toString().c_str();
        if (name=="wrapped"||(name.strstr(".wrapped")>=0)) {
            continue;
        }
        ConstString desc = dd.getComment(name.c_str());
        Value def = dd.getDefaultValue(name.c_str());
        Value actual = dd.getValue(name.c_str());
        String out = "";
        out += name.c_str();
        if (!actual.isNull()) {
            if (actual.toString()!="") {
                out += "=";
                if (actual.toString().length()<40) {
                    out += actual.toString().c_str();
                } else {
                    out += "(value too long)";
                }
            }
        }
        if (!def.isNull()) {
            if (def.toString()!="") {
                out += " [";
                if (def.toString().length()<40) {
                    out += def.toString().c_str();
                } else {
                    out += "(value too long)";
                }
                out += "]";
            }
        }
        if (desc!="") {
            out += "\n    ";
            out += desc.c_str();
        }
        os << out.c_str() << endl;
    }
    os << "==" << endl;
    os << "===============================================================" 
       << endl;
}


static ConstString terminatorKey = "";
static bool terminated = false;
static void handler (int) {
    static int ct = 0;
    ct++;
    if (ct>3) {
        printf("Aborting...\n");
        ACE_OS::exit(1);
    }
    if (terminatorKey!="") {
        printf("[try %d of 3] Trying to shut down %s\n", 
               ct,
               terminatorKey.c_str());
        terminated = true;
        //Terminator::terminateByName(terminatorKey.c_str());
    }
}


int Drivers::yarpdev(int argc, char *argv[]) {

    ACE_OS::signal(SIGINT, (ACE_SignalHandler) handler);

    // get command line options
    Property options;

    // yarpdev will by default try to pass its thread on to the device.
    // this is because some libraries need configuration and all
    // access methods done in the same thread (e.g. opencv_grabber
    // apparently).
    options.put("single_threaded", 1);

    // interpret as a set of flags
    options.fromCommand(argc,argv,true,false);

    // check if we're being asked to read the options from file
    Value *val;
    if (options.check("file",val)) {
        ConstString fname = val->toString();
        options.unput("file");
        printf("yarpdev: working with config file %s\n", fname.c_str());
        options.fromConfigFile(fname,false);
    }

    // check if we want to use nested options (less ambiguous)
    if (options.check("nested",val)||options.check("lispy",val)) {
        ConstString lispy = val->toString();
        printf("yarpdev: working with config %s\n", lispy.c_str());
        options.fromString(lispy);
    }

    if (!options.check("device")) {
        // no device mentioned - maybe user needs help
        
        if (options.check("list")) {
            printf("Here are devices listed for your system:\n");
            printf("%s", Drivers::factory().toString().c_str());
        } else {
            printf("Welcome to yarpdev, a program to create YARP devices\n");
            printf("To see the devices available, try:\n");
            printf("   yarpdev --list\n");
            printf("To create a device whose name you know, call yarpdev like this:\n");
            printf("   yarpdev --device DEVICENAME --OPTION VALUE ...\n");
            printf("For example:\n");
            printf("   yarpdev --device test_grabber --width 32 --height 16 --name /grabber\n");
            printf("You can always move options to a configuration file:\n");
            printf("   yarpdev [--device DEVICENAME] --file CONFIG_FILENAME\n");
            printf("If you have problems, you can add the \"verbose\" flag to get more information\n");
            printf("   yarpdev --verbose --device ffmpeg_grabber\n");
        }
        return 0;
    }

    // ask for a wrapped, remotable device rather than raw device
    options.put("wrapped","1");

    //YarpDevMonitor monitor;
    bool verbose = false;
    if (options.check("verbose")) {
        verbose = true;
        //options.setMonitor(&monitor,"top-level");
    }
    PolyDriver dd(options);
    if (verbose) {
        toDox(dd,cout);
    }
    if (!dd.isValid()) {
        printf("yarpdev: ***ERROR*** device not available.\n");
        if (argc==1) { 
            printf("Here are the known devices:\n");
            printf("%s", Drivers::factory().toString().c_str());
        } else {
            printf("Suggestions:\n");
            printf("+ Do \"yarpdev --list\" to see list of supported devices.\n");
            printf("+ Or append \"--verbose\" option to get more information.\n");
        }
        return 1;
    }

    Terminee *terminee = 0;
    if (dd.isValid()) {
        Value *v;
        // default to /argv[0]/quit
        yarp::String s("/");
        s += argv[0];
        s += "/quit";
        if (options.check("name", v)) {
            s.clear();
            s += v->toString().c_str();
            s += "/quit";
        }
        terminee = new Terminee(s.c_str());
        terminatorKey = s.c_str();
        if (terminee == 0) {
            printf("Can't allocate terminator socket port\n");
            return 1;
        }
        if (!terminee->isOk()) {
            printf("Failed to create terminator socket port\n");
            return 1;
        }
    }

    double startTime = Time::now();
    IService *service = NULL;
    dd.view(service);
    if (service!=NULL) {
        bool backgrounded = service->startService();
        if (backgrounded) {
            // we don't need to poll this, so forget about the
            // service interface
            printf("yarpdev: service backgrounded\n");
            service = NULL;
        }
    }
    while (dd.isValid() && !(terminated||terminee->mustQuit())) {
        if (service!=NULL) {
            double now = Time::now();
            double dnow = 1;
            if (now-startTime>dnow) {
                printf("yarpdev: device active...\n");
                startTime += dnow;
            }
            // we requested single threading, so need to
            // give the device its chance
            service->updateService();
        } else {
            // we don't need to do anything
            printf("yarpdev: device active in background...\n");
            Time::delay(1);
        }
    }

    delete terminee;
    dd.close();

    printf("yarpdev is finished.\n");

    return 0;
}





// defined in PopulateDrivers.cpp:
//   DeviceDriver *Drivers::init()



