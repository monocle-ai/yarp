// -*- mode:C++; tab-width:4; c-basic-offset:4; indent-tabs-mode:nil -*-

#ifndef _YARP2_WIRE_
#define _YARP2_WIRE_

#include <yarp/os/WireLink.h>

namespace yarp {
    namespace os {
        class Wire;
    }
}

/**
 *
 * Base class for IDL client/server.
 *
 */
class yarp::os::Wire : public PortReader {
private:
    WireLink _yarp_link;
public:
    /**
     *
     * Get YARP state associated with this object.
     *
     * @return state object.
     *
     */
    WireLink& yarp() { return _yarp_link; }
};

#endif
