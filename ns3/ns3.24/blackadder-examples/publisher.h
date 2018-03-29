/*
 * Copyright (C) 2010-2012  George Parisis and Dirk Trossen
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 3 as published by the Free Software Foundation.
 *
 * See LICENSE and COPYING for more details.
 */

#ifndef PUBLISHER_H
#define PUBLISHER_H

#include <ns3/core-module.h>
#include <ns3/network-module.h>
#include <ns3/blackadder-module.h>
#include <ns3/random-variable-stream.h>     //Derived classes of RandomVariable (i.e. the random variable implementations) have been
                                            //ported to a new RandomVariableStream base class in NS3.15 and above.
namespace ns3 {

    /**@brief A sample publisher application. As all Blackadder NS3 applications, it must extend the PubSubApplication Class.
     * 
     */
    class Publisher : public PubSubApplication {
    public:
        /**@brief As mandated by NS3..no attributes.
         */
        static TypeId GetTypeId(void);
        /**@brief Constructor: it initializes the single NS3 event in the code. The next time at which the publisher will advertise a new information item. 
         */
        Publisher();
        virtual ~Publisher();
        /**@brief The EventListener that is registered with the ServiceModel Class. It asynchronously called when a Blackadder Event is received. 
         * When a START_PUBLISH Event is received, this publisher will immediately (no simulation time is passed) publish the respective dummy data.
         * 
         * @param ev a smart pointer to the received Event.
         */
        void EventHandler(Ptr<Event> ev);
    protected:
        void DoInitialize(void);         //Object and subclasses DoStart has been renamed to DoInitialize in NS3.17 and above.          
        virtual void DoDispose(void);
    private:
        /**@brief When the publisher is started, it advertises root scope 00000000000 and schedules (at 0 seconds) an NS3 event. 
         * Each time this event is scheduled, the publisher will advertise a randomly generated information identifier.
         * 
         */
        virtual void StartApplication(void);
        /**@brief It cancels any scheduled NS3 event.
         */
        virtual void StopApplication(void);
        /**@
         * 
         */
        void publish(void);

        Time m_interval;
        EventId m_Event;

        Ptr<UniformRandomVariable> part1;   // Variable Distribution Class (UniformVariable) has been replaced with 
                                            // (UniformRandomVariable) from NS3.15 and above.
    };

} // namespace ns3

#endif
