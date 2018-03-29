/*
 * Copyright (C) 2010-2011  George Parisis and Dirk Trossen
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 3 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * See LICENSE and COPYING for more details.
 */

#ifndef CLICK_NETLINK_HH
#define CLICK_NETLINK_HH
#include <../lib/blackadder_enums.hpp>
#include <click/config.h>
#include <click/element.hh>
#include <click/confparse.hh>
#include <click/error.hh>
#include <click/task.hh>

#ifdef __linux__
#define HAVE_USE_NETLINK 1
#endif

#if !HAVE_USE_NETLINK && !CLICK_BSDMODULE
#define HAVE_USE_UNIX 1
#endif

#if CLICK_LINUXMODULE || CLICK_BSDMODULE
#include <click/hashtable.hh>
#include <click/standard/scheduleinfo.hh>
#include <click/cxxprotect.h>
CLICK_CXX_PROTECT
# if CLICK_LINUXMODULE
#include <linux/pid.h>
#include <linux/netlink.h>
#include <linux/module.h>
#include <net/sock.h>
# else
#include "bsd_ba_socket.h"
# endif
CLICK_CXX_UNPROTECT
#include <click/cxxunprotect.h>
#define TASK_IS_SCHEDULED 0
#else
#include <queue>
#include <click/cxxprotect.h>
CLICK_CXX_PROTECT
# if HAVE_USE_NETLINK
#include <linux/netlink.h>
# elif HAVE_USE_UNIX  /* __FreeBSD__, __APPLE__, etc. */
#  ifdef __linux__
#include <linux/un.h>
#  else
#include <sys/un.h>
#  endif
# endif
# ifdef __FreeBSD__
# include <sys/event.h>
# endif
CLICK_CXX_UNPROTECT
#include <click/cxxunprotect.h>
#endif

CLICK_DECLS


/**@brief (Blackadder Core) The Netlink Element is the base element that creates, opens and binds to the netlink socket of Blackadder.
 * 
 * It does not receive or push packets. It is passed as a paramater to the FromNetlink and ToNetlink Elements that do the actual job.
 */
class Netlink : public Element {
public:
    /**
     * Constructor: it does nothing - as Click suggests
     * @return 
     */
    Netlink();
    /**
     * Destructor: it does nothing - as Click suggests
     * @return 
     */
    ~Netlink();
    /**
     * the class name - required by Click
     * @return 
     */
    const char *class_name() const {return "Netlink";}
    /**
     * the port count - required by Click - there are no input or output ports.
     * @return 
     */
    const char *port_count() const {return "0/0";}
    /**
     * an AGNOSTIC Element - actually it does not process at all - is there a better value?
     * @return AGNOSTIC
     */
    const char *processing() const {return AGNOSTIC;}
    /** This Element must be configured before the FromNetlink and ToNetlink
     * 
     * @return the correct number so that it is configured before
     */
    int configure_phase() const {
        return 300;
    }
    /**
     * @brief This method is called by Click when the Element is about to be initialized.
     * 
     * In user space the socket is opened here. It binds to the port 9999 which should be the one used by all applications.
     * The netlink protocol is NETLINK_GENERIC
     * In kernel space the kernel socket is initialized in the FromNetlink Element so that the callback defined there is passed as an argument.
     * @param errh
     * @return 
     */
    int initialize(ErrorHandler *errh);
    /**@brief Cleanups everything.
     * 
     * If the stage is before CLEANUP_INITIALIZED, then it does nothing, else it releases the socket in both user and kernel space.
     * @param stage
     */
    void cleanup(CleanupStage stage);
    /*members*/
#if CLICK_LINUXMODULE
    /** the struct socket *, which represents the kernel netlink socket
     */
    struct sock *nl_sk;
#elif CLICK_BSDMODULE
#else
    /**the netlink socket descriptor
     */
    int fd;
    /* mfhaln: the netlink socket descriptor for management traffic
     */
    int management_fd;
#if HAVE_USE_UNIX
    struct sockaddr_un s_nladdr;
    /* mfhaln: the netlink socket address for management traffic
     */
    struct sockaddr_un management_s_nladdr;
#endif
    /** a queue (from STL to use only in user space) that holds the packets to be sent to an application via the netlink socket.
     */
    std::queue <WritablePacket *> out_buf_queue;
#endif
};

CLICK_ENDDECLS

#ifndef __LINUX_NETLINK_H
extern "C" {
struct nlmsghdr {
    uint32_t    nlmsg_len;
    uint16_t    nlmsg_type;
    uint16_t    nlmsg_flags;
    uint32_t    nlmsg_seq;
    uint32_t    nlmsg_pid;
};

struct sockaddr_nl {
    sa_family_t     nl_family;
    unsigned short  nl_pad;
    uint32_t        nl_pid;
    uint32_t        nl_groups;
};
}
#endif

#if HAVE_USE_UNIX
#define ba_id2path(path, id)  \
    snprintf((path), 100, "/tmp/blackadder.%05u", (id))
#if defined(__FreeBSD__) || defined(__OpenBSD__)
inline int
ba_path2id(const char *path)
{
    const char *errstr = NULL;
    int num = (int)strtonum((path) + 16, 0, 99999/*PID_MAX*/, &errstr);
    return (errstr == NULL) ? num : -1;
}
#else
#define ba_path2id(path) atoi((path) + 16)
#endif
#endif /* HAVE_USE_UNIX */

#endif /* CLICK_NETLINK_HH */
