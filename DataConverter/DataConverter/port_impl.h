/*
 * This file is protected by Copyright. Please refer to the COPYRIGHT file distributed with this
 * source distribution.
 *
 * This file is part of REDHAWK Basic Components.
 *
 * REDHAWK Basic Components is free software: you can redistribute it and/or modify it under the terms of
 * the GNU Lesser General Public License as published by the Free Software Foundation, either
 * version 3 of the License, or (at your option) any later version.
 *
 * REDHAWK Basic Components is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License along with this
 * program.  If not, see http://www.gnu.org/licenses/.
 */
#ifndef PORT_H
#define PORT_H

#include "ossie/Port_impl.h"
#include "ossie/debug.h"
#include <queue>
#include <list>
#include <boost/thread/condition_variable.hpp>
#include <boost/thread/locks.hpp>

class DataConverter_base;
class DataConverter_i;

#define CORBA_MAX_TRANSFER_BYTES omniORB::giopMaxMsgSize()


#include "BULKIO/bio_dataOctet.h"

#include "BULKIO/bio_dataShort.h"

#include "BULKIO/bio_dataUshort.h"

#include "BULKIO/bio_dataLong.h"

#include "BULKIO/bio_dataUlong.h"

#include "BULKIO/bio_dataFloat.h"

#include "BULKIO/bio_dataDouble.h"

// ----------------------------------------------------------------------------------------
// BULKIO_dataUshort_Out_i declaration
// ----------------------------------------------------------------------------------------
class BULKIO_dataUshort_Out_i : public Port_Uses_base_impl, public virtual POA_BULKIO::UsesPortStatisticsProvider
{
    ENABLE_LOGGING
    public:
        BULKIO_dataUshort_Out_i(std::string port_name, DataConverter_base *_parent);
        ~BULKIO_dataUshort_Out_i();

        void pushSRI(const BULKIO::StreamSRI& H);
        
        /*
         * pushPacket
         *     description: push data out of the port
         *
         *  data: structure containing the payload to send out
         *  T: constant of type BULKIO::PrecisionUTCTime containing the timestamp for the outgoing data.
         *    tcmode: timecode mode
         *    tcstatus: timecode status 
         *    toff: fractional sample offset
         *    twsec: J1970 GMT 
         *    tfsec: fractional seconds: 0.0 to 1.0
         *  EOS: end-of-stream flag
         *  streamID: stream identifier
         */
        template <typename ALLOCATOR>
        void pushPacket(std::vector<CORBA::UShort, ALLOCATOR>& data, BULKIO::PrecisionUTCTime& T, bool EOS, const std::string& streamID) {
            if (refreshSRI) {
                if (currentSRIs.find(streamID) == currentSRIs.end()) {
                    BULKIO::StreamSRI sri;
                    sri.hversion = 1;
                    sri.xstart = 0.0;
                    sri.xdelta = 1.0;
                    sri.xunits = BULKIO::UNITS_TIME;
                    sri.subsize = 0;
                    sri.ystart = 0.0;
                    sri.ydelta = 0.0;
                    sri.yunits = BULKIO::UNITS_NONE;
                    sri.mode = 0;
                    sri.blocking = false;
                    sri.streamID = streamID.c_str();
                    currentSRIs[streamID] = sri;
                }
                pushSRI(currentSRIs[streamID]);
            }
            boost::mutex::scoped_lock lock(updatingPortsLock);   // don't want to process while command information is coming in
            // Magic is below, make a new sequence using the data from the Iterator
            // as the data for the sequence.  The 'false' at the end is whether or not
            // CORBA is allowed to delete the buffer when the sequence is destroyed.
            PortTypes::UshortSequence seq = PortTypes::UshortSequence(data.size(), data.size(), &(data[0]), false);
            if (active) {
                std::vector < std::pair < BULKIO::dataUshort_var, std::string > >::iterator port;
                for (port = outConnections.begin(); port != outConnections.end(); port++) {
                    try {
                        ((*port).first)->pushPacket(seq, T, EOS, streamID.c_str());
                        stats[(*port).second].update(data.size(), 0, EOS, streamID);
                    } catch(...) {
                        LOG_ERROR(BULKIO_dataUshort_Out_i, "Call to pushPacket by BULKIO_dataUshort_Out_i failed");
                    }
                }
            }
            // for end of stream,  remove old sri
            try {
                if ( EOS ) currentSRIs.erase(streamID);
            }
            catch(...){
            }

        };
        class linkStatistics
        {
            public:
                struct statPoint {
                    unsigned int elements;
                    float queueSize;
                    double secs;
                    double usecs;
                };
                
                linkStatistics() {
                    bitSize = sizeof(CORBA::UShort) * 8.0;
                    historyWindow = 10;
                    activeStreamIDs.resize(0);
                    receivedStatistics_idx = 0;
                    receivedStatistics.resize(historyWindow);
                    runningStats.elementsPerSecond = -1.0;
                    runningStats.bitsPerSecond = -1.0;
                    runningStats.callsPerSecond = -1.0;
                    runningStats.averageQueueDepth = -1.0;
                    runningStats.streamIDs.length(0);
                    runningStats.timeSinceLastCall = -1;
                    enabled = true;
                };

                void setEnabled(bool enableStats) {
                    enabled = enableStats;
                }

                void update(unsigned int elementsReceived, float queueSize, bool EOS, std::string streamID) {
                    if (!enabled) {
                        return;
                    }
                    struct timeval tv;
                    struct timezone tz;
                    gettimeofday(&tv, &tz);
                    receivedStatistics[receivedStatistics_idx].elements = elementsReceived;
                    receivedStatistics[receivedStatistics_idx].queueSize = queueSize;
                    receivedStatistics[receivedStatistics_idx].secs = tv.tv_sec;
                    receivedStatistics[receivedStatistics_idx++].usecs = tv.tv_usec;
                    receivedStatistics_idx = receivedStatistics_idx % historyWindow;
                    if (!EOS) {
                        std::list<std::string>::iterator p = activeStreamIDs.begin();
                        bool foundStreamID = false;
                        while (p != activeStreamIDs.end()) {
                            if (*p == streamID) {
                                foundStreamID = true;
                                break;
                            }
                            p++;
                        }
                        if (!foundStreamID) {
                            activeStreamIDs.push_back(streamID);
                        }
                    } else {
                        std::list<std::string>::iterator p = activeStreamIDs.begin();
                        while (p != activeStreamIDs.end()) {
                            if (*p == streamID) {
                                activeStreamIDs.erase(p);
                                break;
                            }
                            p++;
                        }
                    }
                };

                BULKIO::PortStatistics retrieve() {
                    if (!enabled) {
                        return runningStats;
                    }
                    struct timeval tv;
                    struct timezone tz;
                    gettimeofday(&tv, &tz);

                    int idx = (receivedStatistics_idx == 0) ? (historyWindow - 1) : (receivedStatistics_idx - 1);
                    double front_sec = receivedStatistics[idx].secs;
                    double front_usec = receivedStatistics[idx].usecs;
                    double secDiff = tv.tv_sec - receivedStatistics[receivedStatistics_idx].secs;
                    double usecDiff = (tv.tv_usec - receivedStatistics[receivedStatistics_idx].usecs) / ((double)1e6);

                    double totalTime = secDiff + usecDiff;
                    double totalData = 0;
                    float queueSize = 0;
                    int startIdx = (receivedStatistics_idx + 1) % historyWindow;
                    for (int i = startIdx; i != receivedStatistics_idx; ) {
                        totalData += receivedStatistics[i].elements;
                        queueSize += receivedStatistics[i].queueSize;
                        i = (i + 1) % historyWindow;
                    }
                    runningStats.bitsPerSecond = ((totalData * bitSize) / totalTime);
                    runningStats.elementsPerSecond = (totalData / totalTime);
                    runningStats.averageQueueDepth = (queueSize / historyWindow);
                    runningStats.callsPerSecond = (double(historyWindow - 1) / totalTime);
                    runningStats.timeSinceLastCall = (((double)tv.tv_sec) - front_sec) + (((double)tv.tv_usec - front_usec) / ((double)1e6));
                    unsigned int streamIDsize = activeStreamIDs.size();
                    std::list< std::string >::iterator p = activeStreamIDs.begin();
                    runningStats.streamIDs.length(streamIDsize);
                    for (unsigned int i = 0; i < streamIDsize; i++) {
                        if (p == activeStreamIDs.end()) {
                            break;
                        }
                        runningStats.streamIDs[i] = CORBA::string_dup((*p).c_str());
                        p++;
                    }
                    return runningStats;
                };

            protected:
                bool enabled;
                double bitSize;
                BULKIO::PortStatistics runningStats;
                std::vector<statPoint> receivedStatistics;
                std::list< std::string > activeStreamIDs;
                unsigned long historyWindow;
                int receivedStatistics_idx;
        };

        BULKIO::UsesPortStatisticsSequence * statistics()
        {
            boost::mutex::scoped_lock lock(updatingPortsLock);
            BULKIO::UsesPortStatisticsSequence_var recStat = new BULKIO::UsesPortStatisticsSequence();
            recStat->length(outConnections.size());
            for (unsigned int i = 0; i < outConnections.size(); i++) {
                recStat[i].connectionId = CORBA::string_dup(outConnections[i].second.c_str());
                recStat[i].statistics = stats[outConnections[i].second].retrieve();
            }
            return recStat._retn();
        };

        BULKIO::PortUsageType state()
        {
            boost::mutex::scoped_lock lock(updatingPortsLock);
            if (outConnections.size() > 0) {
                return BULKIO::ACTIVE;
            } else {
                return BULKIO::IDLE;
            }

            return BULKIO::BUSY;
        };
        
        void enableStats(bool enable)
        {
            for (unsigned int i = 0; i < outConnections.size(); i++) {
                stats[outConnections[i].second].setEnabled(enable);
            }
        };


        ExtendedCF::UsesConnectionSequence * connections() 
        {
            boost::mutex::scoped_lock lock(updatingPortsLock);   // don't want to process while command information is coming in
            if (recConnectionsRefresh) {
                recConnections.length(outConnections.size());
                for (unsigned int i = 0; i < outConnections.size(); i++) {
                    recConnections[i].connectionId = CORBA::string_dup(outConnections[i].second.c_str());
                    recConnections[i].port = CORBA::Object::_duplicate(outConnections[i].first);
                }
                recConnectionsRefresh = false;
            }
            ExtendedCF::UsesConnectionSequence_var retVal = new ExtendedCF::UsesConnectionSequence(recConnections);
            // NOTE: You must delete the object that this function returns!
            return retVal._retn();
        };

        void connectPort(CORBA::Object_ptr connection, const char* connectionId)
        {
            boost::mutex::scoped_lock lock(updatingPortsLock);   // don't want to process while command information is coming in
            BULKIO::dataUshort_var port = BULKIO::dataUshort::_narrow(connection);
            outConnections.push_back(std::make_pair(port, connectionId));
            active = true;
            recConnectionsRefresh = true;
            refreshSRI = true;
        };

        void disconnectPort(const char* connectionId)
        {
            boost::mutex::scoped_lock lock(updatingPortsLock);   // don't want to process while command information is coming in
            for (unsigned int i = 0; i < outConnections.size(); i++) {
                if (outConnections[i].second == connectionId) {
                    outConnections.erase(outConnections.begin() + i);
                    break;
                }
            }

            if (outConnections.size() == 0) {
                active = false;
            }
            recConnectionsRefresh = true;
        };

        std::vector< std::pair<BULKIO::dataUshort_var, std::string> > _getConnections()
        {
            return outConnections;
        };
        std::map<std::string, BULKIO::StreamSRI> currentSRIs;

    protected:
        DataConverter_i *parent;
        std::vector < std::pair<BULKIO::dataUshort_var, std::string> > outConnections;
        ExtendedCF::UsesConnectionSequence recConnections;
        bool recConnectionsRefresh;
        std::map<std::string, linkStatistics> stats;
};


// ----------------------------------------------------------------------------------------
// BULKIO_dataShort_Out_i declaration
// ----------------------------------------------------------------------------------------
class BULKIO_dataShort_Out_i : public Port_Uses_base_impl, public virtual POA_BULKIO::UsesPortStatisticsProvider
{
    ENABLE_LOGGING
    public:
        BULKIO_dataShort_Out_i(std::string port_name, DataConverter_base *_parent);
        ~BULKIO_dataShort_Out_i();

        void pushSRI(const BULKIO::StreamSRI& H);
        
        /*
         * pushPacket
         *     description: push data out of the port
         *
         *  data: structure containing the payload to send out
         *  T: constant of type BULKIO::PrecisionUTCTime containing the timestamp for the outgoing data.
         *    tcmode: timecode mode
         *    tcstatus: timecode status 
         *    toff: fractional sample offset
         *    twsec: J1970 GMT 
         *    tfsec: fractional seconds: 0.0 to 1.0
         *  EOS: end-of-stream flag
         *  streamID: stream identifier
         */
        template <typename ALLOCATOR>
        void pushPacket(std::vector<CORBA::Short, ALLOCATOR>& data, BULKIO::PrecisionUTCTime& T, bool EOS, const std::string& streamID) {
            if (refreshSRI) {
                if (currentSRIs.find(streamID) == currentSRIs.end()) {
                    BULKIO::StreamSRI sri;
                    sri.hversion = 1;
                    sri.xstart = 0.0;
                    sri.xdelta = 1.0;
                    sri.xunits = BULKIO::UNITS_TIME;
                    sri.subsize = 0;
                    sri.ystart = 0.0;
                    sri.ydelta = 0.0;
                    sri.yunits = BULKIO::UNITS_NONE;
                    sri.mode = 0;
                    sri.blocking = false;
                    sri.streamID = streamID.c_str();
                    currentSRIs[streamID] = sri;
                }
                pushSRI(currentSRIs[streamID]);
            }
            boost::mutex::scoped_lock lock(updatingPortsLock);   // don't want to process while command information is coming in
            // Magic is below, make a new sequence using the data from the Iterator
            // as the data for the sequence.  The 'false' at the end is whether or not
            // CORBA is allowed to delete the buffer when the sequence is destroyed.
            PortTypes::ShortSequence seq = PortTypes::ShortSequence(data.size(), data.size(), &(data[0]), false);
            if (active) {
                std::vector < std::pair < BULKIO::dataShort_var, std::string > >::iterator port;
                for (port = outConnections.begin(); port != outConnections.end(); port++) {
                    try {
                        ((*port).first)->pushPacket(seq, T, EOS, streamID.c_str());
                        stats[(*port).second].update(data.size(), 0, EOS, streamID);
                    } catch(...) {
                        LOG_ERROR(BULKIO_dataShort_Out_i, "Call to pushPacket by BULKIO_dataShort_Out_i failed");
                    }
                }
            }
            // for end of stream,  remove old sri
            try {
                if ( EOS ) currentSRIs.erase(streamID);
            }
            catch(...){
            }

        };
        class linkStatistics
        {
            public:
                struct statPoint {
                    unsigned int elements;
                    float queueSize;
                    double secs;
                    double usecs;
                };
                
                linkStatistics() {
                    bitSize = sizeof(CORBA::Short) * 8.0;
                    historyWindow = 10;
                    activeStreamIDs.resize(0);
                    receivedStatistics_idx = 0;
                    receivedStatistics.resize(historyWindow);
                    runningStats.elementsPerSecond = -1.0;
                    runningStats.bitsPerSecond = -1.0;
                    runningStats.callsPerSecond = -1.0;
                    runningStats.averageQueueDepth = -1.0;
                    runningStats.streamIDs.length(0);
                    runningStats.timeSinceLastCall = -1;
                    enabled = true;
                };

                void setEnabled(bool enableStats) {
                    enabled = enableStats;
                }

                void update(unsigned int elementsReceived, float queueSize, bool EOS, std::string streamID) {
                    if (!enabled) {
                        return;
                    }
                    struct timeval tv;
                    struct timezone tz;
                    gettimeofday(&tv, &tz);
                    receivedStatistics[receivedStatistics_idx].elements = elementsReceived;
                    receivedStatistics[receivedStatistics_idx].queueSize = queueSize;
                    receivedStatistics[receivedStatistics_idx].secs = tv.tv_sec;
                    receivedStatistics[receivedStatistics_idx++].usecs = tv.tv_usec;
                    receivedStatistics_idx = receivedStatistics_idx % historyWindow;
                    if (!EOS) {
                        std::list<std::string>::iterator p = activeStreamIDs.begin();
                        bool foundStreamID = false;
                        while (p != activeStreamIDs.end()) {
                            if (*p == streamID) {
                                foundStreamID = true;
                                break;
                            }
                            p++;
                        }
                        if (!foundStreamID) {
                            activeStreamIDs.push_back(streamID);
                        }
                    } else {
                        std::list<std::string>::iterator p = activeStreamIDs.begin();
                        while (p != activeStreamIDs.end()) {
                            if (*p == streamID) {
                                activeStreamIDs.erase(p);
                                break;
                            }
                            p++;
                        }
                    }
                };

                BULKIO::PortStatistics retrieve() {
                    if (!enabled) {
                        return runningStats;
                    }
                    struct timeval tv;
                    struct timezone tz;
                    gettimeofday(&tv, &tz);

                    int idx = (receivedStatistics_idx == 0) ? (historyWindow - 1) : (receivedStatistics_idx - 1);
                    double front_sec = receivedStatistics[idx].secs;
                    double front_usec = receivedStatistics[idx].usecs;
                    double secDiff = tv.tv_sec - receivedStatistics[receivedStatistics_idx].secs;
                    double usecDiff = (tv.tv_usec - receivedStatistics[receivedStatistics_idx].usecs) / ((double)1e6);

                    double totalTime = secDiff + usecDiff;
                    double totalData = 0;
                    float queueSize = 0;
                    int startIdx = (receivedStatistics_idx + 1) % historyWindow;
                    for (int i = startIdx; i != receivedStatistics_idx; ) {
                        totalData += receivedStatistics[i].elements;
                        queueSize += receivedStatistics[i].queueSize;
                        i = (i + 1) % historyWindow;
                    }
                    runningStats.bitsPerSecond = ((totalData * bitSize) / totalTime);
                    runningStats.elementsPerSecond = (totalData / totalTime);
                    runningStats.averageQueueDepth = (queueSize / historyWindow);
                    runningStats.callsPerSecond = (double(historyWindow - 1) / totalTime);
                    runningStats.timeSinceLastCall = (((double)tv.tv_sec) - front_sec) + (((double)tv.tv_usec - front_usec) / ((double)1e6));
                    unsigned int streamIDsize = activeStreamIDs.size();
                    std::list< std::string >::iterator p = activeStreamIDs.begin();
                    runningStats.streamIDs.length(streamIDsize);
                    for (unsigned int i = 0; i < streamIDsize; i++) {
                        if (p == activeStreamIDs.end()) {
                            break;
                        }
                        runningStats.streamIDs[i] = CORBA::string_dup((*p).c_str());
                        p++;
                    }
                    return runningStats;
                };

            protected:
                bool enabled;
                double bitSize;
                BULKIO::PortStatistics runningStats;
                std::vector<statPoint> receivedStatistics;
                std::list< std::string > activeStreamIDs;
                unsigned long historyWindow;
                int receivedStatistics_idx;
        };

        BULKIO::UsesPortStatisticsSequence * statistics()
        {
            boost::mutex::scoped_lock lock(updatingPortsLock);
            BULKIO::UsesPortStatisticsSequence_var recStat = new BULKIO::UsesPortStatisticsSequence();
            recStat->length(outConnections.size());
            for (unsigned int i = 0; i < outConnections.size(); i++) {
                recStat[i].connectionId = CORBA::string_dup(outConnections[i].second.c_str());
                recStat[i].statistics = stats[outConnections[i].second].retrieve();
            }
            return recStat._retn();
        };

        BULKIO::PortUsageType state()
        {
            boost::mutex::scoped_lock lock(updatingPortsLock);
            if (outConnections.size() > 0) {
                return BULKIO::ACTIVE;
            } else {
                return BULKIO::IDLE;
            }

            return BULKIO::BUSY;
        };
        
        void enableStats(bool enable)
        {
            for (unsigned int i = 0; i < outConnections.size(); i++) {
                stats[outConnections[i].second].setEnabled(enable);
            }
        };


        ExtendedCF::UsesConnectionSequence * connections() 
        {
            boost::mutex::scoped_lock lock(updatingPortsLock);   // don't want to process while command information is coming in
            if (recConnectionsRefresh) {
                recConnections.length(outConnections.size());
                for (unsigned int i = 0; i < outConnections.size(); i++) {
                    recConnections[i].connectionId = CORBA::string_dup(outConnections[i].second.c_str());
                    recConnections[i].port = CORBA::Object::_duplicate(outConnections[i].first);
                }
                recConnectionsRefresh = false;
            }
            ExtendedCF::UsesConnectionSequence_var retVal = new ExtendedCF::UsesConnectionSequence(recConnections);
            // NOTE: You must delete the object that this function returns!
            return retVal._retn();
        };

        void connectPort(CORBA::Object_ptr connection, const char* connectionId)
        {
            boost::mutex::scoped_lock lock(updatingPortsLock);   // don't want to process while command information is coming in
            BULKIO::dataShort_var port = BULKIO::dataShort::_narrow(connection);
            outConnections.push_back(std::make_pair(port, connectionId));
            active = true;
            recConnectionsRefresh = true;
            refreshSRI = true;
        };

        void disconnectPort(const char* connectionId)
        {
            boost::mutex::scoped_lock lock(updatingPortsLock);   // don't want to process while command information is coming in
            for (unsigned int i = 0; i < outConnections.size(); i++) {
                if (outConnections[i].second == connectionId) {
                    outConnections.erase(outConnections.begin() + i);
                    break;
                }
            }

            if (outConnections.size() == 0) {
                active = false;
            }
            recConnectionsRefresh = true;
        };

        std::vector< std::pair<BULKIO::dataShort_var, std::string> > _getConnections()
        {
            return outConnections;
        };
        std::map<std::string, BULKIO::StreamSRI> currentSRIs;

    protected:
        DataConverter_i *parent;
        std::vector < std::pair<BULKIO::dataShort_var, std::string> > outConnections;
        ExtendedCF::UsesConnectionSequence recConnections;
        bool recConnectionsRefresh;
        std::map<std::string, linkStatistics> stats;
};


// ----------------------------------------------------------------------------------------
// BULKIO_dataUlong_Out_i declaration
// ----------------------------------------------------------------------------------------
class BULKIO_dataUlong_Out_i : public Port_Uses_base_impl, public virtual POA_BULKIO::UsesPortStatisticsProvider
{
    ENABLE_LOGGING
    public:
        BULKIO_dataUlong_Out_i(std::string port_name, DataConverter_base *_parent);
        ~BULKIO_dataUlong_Out_i();

        void pushSRI(const BULKIO::StreamSRI& H);
        
        /*
         * pushPacket
         *     description: push data out of the port
         *
         *  data: structure containing the payload to send out
         *  T: constant of type BULKIO::PrecisionUTCTime containing the timestamp for the outgoing data.
         *    tcmode: timecode mode
         *    tcstatus: timecode status 
         *    toff: fractional sample offset
         *    twsec: J1970 GMT 
         *    tfsec: fractional seconds: 0.0 to 1.0
         *  EOS: end-of-stream flag
         *  streamID: stream identifier
         */
        template <typename ALLOCATOR>
        void pushPacket(std::vector<CORBA::ULong, ALLOCATOR>& data, BULKIO::PrecisionUTCTime& T, bool EOS, const std::string& streamID) {
            if (refreshSRI) {
                if (currentSRIs.find(streamID) == currentSRIs.end()) {
                    BULKIO::StreamSRI sri;
                    sri.hversion = 1;
                    sri.xstart = 0.0;
                    sri.xdelta = 1.0;
                    sri.xunits = BULKIO::UNITS_TIME;
                    sri.subsize = 0;
                    sri.ystart = 0.0;
                    sri.ydelta = 0.0;
                    sri.yunits = BULKIO::UNITS_NONE;
                    sri.mode = 0;
                    sri.blocking = false;
                    sri.streamID = streamID.c_str();
                    currentSRIs[streamID] = sri;
                }
                pushSRI(currentSRIs[streamID]);
            }
            boost::mutex::scoped_lock lock(updatingPortsLock);   // don't want to process while command information is coming in
            // Magic is below, make a new sequence using the data from the Iterator
            // as the data for the sequence.  The 'false' at the end is whether or not
            // CORBA is allowed to delete the buffer when the sequence is destroyed.
            PortTypes::UlongSequence seq = PortTypes::UlongSequence(data.size(), data.size(), (CORBA::ULong*)&(data[0]), false);
            if (active) {
                std::vector < std::pair < BULKIO::dataUlong_var, std::string > >::iterator port;
                for (port = outConnections.begin(); port != outConnections.end(); port++) {
                    try {
                        ((*port).first)->pushPacket(seq, T, EOS, streamID.c_str());
                        stats[(*port).second].update(data.size(), 0, EOS, streamID);
                    } catch(...) {
                        LOG_ERROR(BULKIO_dataUlong_Out_i, "Call to pushPacket by BULKIO_dataUlong_Out_i failed");
                    }
                }
            }
            // for end of stream,  remove old sri
            try {
                if ( EOS ) currentSRIs.erase(streamID);
            }
            catch(...){
            }

        };
        class linkStatistics
        {
            public:
                struct statPoint {
                    unsigned int elements;
                    float queueSize;
                    double secs;
                    double usecs;
                };
                
                linkStatistics() {
                    bitSize = sizeof(CORBA::ULong) * 8.0;
                    historyWindow = 10;
                    activeStreamIDs.resize(0);
                    receivedStatistics_idx = 0;
                    receivedStatistics.resize(historyWindow);
                    runningStats.elementsPerSecond = -1.0;
                    runningStats.bitsPerSecond = -1.0;
                    runningStats.callsPerSecond = -1.0;
                    runningStats.averageQueueDepth = -1.0;
                    runningStats.streamIDs.length(0);
                    runningStats.timeSinceLastCall = -1;
                    enabled = true;
                };

                void setEnabled(bool enableStats) {
                    enabled = enableStats;
                }

                void update(unsigned int elementsReceived, float queueSize, bool EOS, std::string streamID) {
                    if (!enabled) {
                        return;
                    }
                    struct timeval tv;
                    struct timezone tz;
                    gettimeofday(&tv, &tz);
                    receivedStatistics[receivedStatistics_idx].elements = elementsReceived;
                    receivedStatistics[receivedStatistics_idx].queueSize = queueSize;
                    receivedStatistics[receivedStatistics_idx].secs = tv.tv_sec;
                    receivedStatistics[receivedStatistics_idx++].usecs = tv.tv_usec;
                    receivedStatistics_idx = receivedStatistics_idx % historyWindow;
                    if (!EOS) {
                        std::list<std::string>::iterator p = activeStreamIDs.begin();
                        bool foundStreamID = false;
                        while (p != activeStreamIDs.end()) {
                            if (*p == streamID) {
                                foundStreamID = true;
                                break;
                            }
                            p++;
                        }
                        if (!foundStreamID) {
                            activeStreamIDs.push_back(streamID);
                        }
                    } else {
                        std::list<std::string>::iterator p = activeStreamIDs.begin();
                        while (p != activeStreamIDs.end()) {
                            if (*p == streamID) {
                                activeStreamIDs.erase(p);
                                break;
                            }
                            p++;
                        }
                    }
                };

                BULKIO::PortStatistics retrieve() {
                    if (!enabled) {
                        return runningStats;
                    }
                    struct timeval tv;
                    struct timezone tz;
                    gettimeofday(&tv, &tz);

                    int idx = (receivedStatistics_idx == 0) ? (historyWindow - 1) : (receivedStatistics_idx - 1);
                    double front_sec = receivedStatistics[idx].secs;
                    double front_usec = receivedStatistics[idx].usecs;
                    double secDiff = tv.tv_sec - receivedStatistics[receivedStatistics_idx].secs;
                    double usecDiff = (tv.tv_usec - receivedStatistics[receivedStatistics_idx].usecs) / ((double)1e6);

                    double totalTime = secDiff + usecDiff;
                    double totalData = 0;
                    float queueSize = 0;
                    int startIdx = (receivedStatistics_idx + 1) % historyWindow;
                    for (int i = startIdx; i != receivedStatistics_idx; ) {
                        totalData += receivedStatistics[i].elements;
                        queueSize += receivedStatistics[i].queueSize;
                        i = (i + 1) % historyWindow;
                    }
                    runningStats.bitsPerSecond = ((totalData * bitSize) / totalTime);
                    runningStats.elementsPerSecond = (totalData / totalTime);
                    runningStats.averageQueueDepth = (queueSize / historyWindow);
                    runningStats.callsPerSecond = (double(historyWindow - 1) / totalTime);
                    runningStats.timeSinceLastCall = (((double)tv.tv_sec) - front_sec) + (((double)tv.tv_usec - front_usec) / ((double)1e6));
                    unsigned int streamIDsize = activeStreamIDs.size();
                    std::list< std::string >::iterator p = activeStreamIDs.begin();
                    runningStats.streamIDs.length(streamIDsize);
                    for (unsigned int i = 0; i < streamIDsize; i++) {
                        if (p == activeStreamIDs.end()) {
                            break;
                        }
                        runningStats.streamIDs[i] = CORBA::string_dup((*p).c_str());
                        p++;
                    }
                    return runningStats;
                };

            protected:
                bool enabled;
                double bitSize;
                BULKIO::PortStatistics runningStats;
                std::vector<statPoint> receivedStatistics;
                std::list< std::string > activeStreamIDs;
                unsigned long historyWindow;
                int receivedStatistics_idx;
        };

        BULKIO::UsesPortStatisticsSequence * statistics()
        {
            boost::mutex::scoped_lock lock(updatingPortsLock);
            BULKIO::UsesPortStatisticsSequence_var recStat = new BULKIO::UsesPortStatisticsSequence();
            recStat->length(outConnections.size());
            for (unsigned int i = 0; i < outConnections.size(); i++) {
                recStat[i].connectionId = CORBA::string_dup(outConnections[i].second.c_str());
                recStat[i].statistics = stats[outConnections[i].second].retrieve();
            }
            return recStat._retn();
        };

        BULKIO::PortUsageType state()
        {
            boost::mutex::scoped_lock lock(updatingPortsLock);
            if (outConnections.size() > 0) {
                return BULKIO::ACTIVE;
            } else {
                return BULKIO::IDLE;
            }

            return BULKIO::BUSY;
        };
        
        void enableStats(bool enable)
        {
            for (unsigned int i = 0; i < outConnections.size(); i++) {
                stats[outConnections[i].second].setEnabled(enable);
            }
        };


        ExtendedCF::UsesConnectionSequence * connections() 
        {
            boost::mutex::scoped_lock lock(updatingPortsLock);   // don't want to process while command information is coming in
            if (recConnectionsRefresh) {
                recConnections.length(outConnections.size());
                for (unsigned int i = 0; i < outConnections.size(); i++) {
                    recConnections[i].connectionId = CORBA::string_dup(outConnections[i].second.c_str());
                    recConnections[i].port = CORBA::Object::_duplicate(outConnections[i].first);
                }
                recConnectionsRefresh = false;
            }
            ExtendedCF::UsesConnectionSequence_var retVal = new ExtendedCF::UsesConnectionSequence(recConnections);
            // NOTE: You must delete the object that this function returns!
            return retVal._retn();
        };

        void connectPort(CORBA::Object_ptr connection, const char* connectionId)
        {
            boost::mutex::scoped_lock lock(updatingPortsLock);   // don't want to process while command information is coming in
            BULKIO::dataUlong_var port = BULKIO::dataUlong::_narrow(connection);
            outConnections.push_back(std::make_pair(port, connectionId));
            active = true;
            recConnectionsRefresh = true;
            refreshSRI = true;
        };

        void disconnectPort(const char* connectionId)
        {
            boost::mutex::scoped_lock lock(updatingPortsLock);   // don't want to process while command information is coming in
            for (unsigned int i = 0; i < outConnections.size(); i++) {
                if (outConnections[i].second == connectionId) {
                    outConnections.erase(outConnections.begin() + i);
                    break;
                }
            }

            if (outConnections.size() == 0) {
                active = false;
            }
            recConnectionsRefresh = true;
        };

        std::vector< std::pair<BULKIO::dataUlong_var, std::string> > _getConnections()
        {
            return outConnections;
        };
        std::map<std::string, BULKIO::StreamSRI> currentSRIs;

    protected:
        DataConverter_i *parent;
        std::vector < std::pair<BULKIO::dataUlong_var, std::string> > outConnections;
        ExtendedCF::UsesConnectionSequence recConnections;
        bool recConnectionsRefresh;
        std::map<std::string, linkStatistics> stats;
};


// ----------------------------------------------------------------------------------------
// BULKIO_dataDouble_Out_i declaration
// ----------------------------------------------------------------------------------------
class BULKIO_dataDouble_Out_i : public Port_Uses_base_impl, public virtual POA_BULKIO::UsesPortStatisticsProvider
{
    ENABLE_LOGGING
    public:
        BULKIO_dataDouble_Out_i(std::string port_name, DataConverter_base *_parent);
        ~BULKIO_dataDouble_Out_i();

        void pushSRI(const BULKIO::StreamSRI& H);
        
        /*
         * pushPacket
         *     description: push data out of the port
         *
         *  data: structure containing the payload to send out
         *  T: constant of type BULKIO::PrecisionUTCTime containing the timestamp for the outgoing data.
         *    tcmode: timecode mode
         *    tcstatus: timecode status 
         *    toff: fractional sample offset
         *    twsec: J1970 GMT 
         *    tfsec: fractional seconds: 0.0 to 1.0
         *  EOS: end-of-stream flag
         *  streamID: stream identifier
         */
        template <typename ALLOCATOR>
        void pushPacket(std::vector<CORBA::Double, ALLOCATOR>& data, BULKIO::PrecisionUTCTime& T, bool EOS, const std::string& streamID) {
            if (refreshSRI) {
                if (currentSRIs.find(streamID) == currentSRIs.end()) {
                    BULKIO::StreamSRI sri;
                    sri.hversion = 1;
                    sri.xstart = 0.0;
                    sri.xdelta = 1.0;
                    sri.xunits = BULKIO::UNITS_TIME;
                    sri.subsize = 0;
                    sri.ystart = 0.0;
                    sri.ydelta = 0.0;
                    sri.yunits = BULKIO::UNITS_NONE;
                    sri.mode = 0;
                    sri.blocking = false;
                    sri.streamID = streamID.c_str();
                    currentSRIs[streamID] = sri;
                }
                pushSRI(currentSRIs[streamID]);
            }
            boost::mutex::scoped_lock lock(updatingPortsLock);   // don't want to process while command information is coming in
            // Magic is below, make a new sequence using the data from the Iterator
            // as the data for the sequence.  The 'false' at the end is whether or not
            // CORBA is allowed to delete the buffer when the sequence is destroyed.
            PortTypes::DoubleSequence seq = PortTypes::DoubleSequence(data.size(), data.size(), &(data[0]), false);
            if (active) {
                std::vector < std::pair < BULKIO::dataDouble_var, std::string > >::iterator port;
                for (port = outConnections.begin(); port != outConnections.end(); port++) {
                    try {
                        ((*port).first)->pushPacket(seq, T, EOS, streamID.c_str());
                        stats[(*port).second].update(data.size(), 0, EOS, streamID);
                    } catch(...) {
                        LOG_ERROR(BULKIO_dataDouble_Out_i, "Call to pushPacket by BULKIO_dataDouble_Out_i failed");
                    }
                }
            }
            // for end of stream,  remove old sri
            try {
                if ( EOS ) currentSRIs.erase(streamID);
            }
            catch(...){
            }

        };
        class linkStatistics
        {
            public:
                struct statPoint {
                    unsigned int elements;
                    float queueSize;
                    double secs;
                    double usecs;
                };
                
                linkStatistics() {
                    bitSize = sizeof(CORBA::Double) * 8.0;
                    historyWindow = 10;
                    activeStreamIDs.resize(0);
                    receivedStatistics_idx = 0;
                    receivedStatistics.resize(historyWindow);
                    runningStats.elementsPerSecond = -1.0;
                    runningStats.bitsPerSecond = -1.0;
                    runningStats.callsPerSecond = -1.0;
                    runningStats.averageQueueDepth = -1.0;
                    runningStats.streamIDs.length(0);
                    runningStats.timeSinceLastCall = -1;
                    enabled = true;
                };

                void setEnabled(bool enableStats) {
                    enabled = enableStats;
                }

                void update(unsigned int elementsReceived, float queueSize, bool EOS, std::string streamID) {
                    if (!enabled) {
                        return;
                    }
                    struct timeval tv;
                    struct timezone tz;
                    gettimeofday(&tv, &tz);
                    receivedStatistics[receivedStatistics_idx].elements = elementsReceived;
                    receivedStatistics[receivedStatistics_idx].queueSize = queueSize;
                    receivedStatistics[receivedStatistics_idx].secs = tv.tv_sec;
                    receivedStatistics[receivedStatistics_idx++].usecs = tv.tv_usec;
                    receivedStatistics_idx = receivedStatistics_idx % historyWindow;
                    if (!EOS) {
                        std::list<std::string>::iterator p = activeStreamIDs.begin();
                        bool foundStreamID = false;
                        while (p != activeStreamIDs.end()) {
                            if (*p == streamID) {
                                foundStreamID = true;
                                break;
                            }
                            p++;
                        }
                        if (!foundStreamID) {
                            activeStreamIDs.push_back(streamID);
                        }
                    } else {
                        std::list<std::string>::iterator p = activeStreamIDs.begin();
                        while (p != activeStreamIDs.end()) {
                            if (*p == streamID) {
                                activeStreamIDs.erase(p);
                                break;
                            }
                            p++;
                        }
                    }
                };

                BULKIO::PortStatistics retrieve() {
                    if (!enabled) {
                        return runningStats;
                    }
                    struct timeval tv;
                    struct timezone tz;
                    gettimeofday(&tv, &tz);

                    int idx = (receivedStatistics_idx == 0) ? (historyWindow - 1) : (receivedStatistics_idx - 1);
                    double front_sec = receivedStatistics[idx].secs;
                    double front_usec = receivedStatistics[idx].usecs;
                    double secDiff = tv.tv_sec - receivedStatistics[receivedStatistics_idx].secs;
                    double usecDiff = (tv.tv_usec - receivedStatistics[receivedStatistics_idx].usecs) / ((double)1e6);

                    double totalTime = secDiff + usecDiff;
                    double totalData = 0;
                    float queueSize = 0;
                    int startIdx = (receivedStatistics_idx + 1) % historyWindow;
                    for (int i = startIdx; i != receivedStatistics_idx; ) {
                        totalData += receivedStatistics[i].elements;
                        queueSize += receivedStatistics[i].queueSize;
                        i = (i + 1) % historyWindow;
                    }
                    runningStats.bitsPerSecond = ((totalData * bitSize) / totalTime);
                    runningStats.elementsPerSecond = (totalData / totalTime);
                    runningStats.averageQueueDepth = (queueSize / historyWindow);
                    runningStats.callsPerSecond = (double(historyWindow - 1) / totalTime);
                    runningStats.timeSinceLastCall = (((double)tv.tv_sec) - front_sec) + (((double)tv.tv_usec - front_usec) / ((double)1e6));
                    unsigned int streamIDsize = activeStreamIDs.size();
                    std::list< std::string >::iterator p = activeStreamIDs.begin();
                    runningStats.streamIDs.length(streamIDsize);
                    for (unsigned int i = 0; i < streamIDsize; i++) {
                        if (p == activeStreamIDs.end()) {
                            break;
                        }
                        runningStats.streamIDs[i] = CORBA::string_dup((*p).c_str());
                        p++;
                    }
                    return runningStats;
                };

            protected:
                bool enabled;
                double bitSize;
                BULKIO::PortStatistics runningStats;
                std::vector<statPoint> receivedStatistics;
                std::list< std::string > activeStreamIDs;
                unsigned long historyWindow;
                int receivedStatistics_idx;
        };

        BULKIO::UsesPortStatisticsSequence * statistics()
        {
            boost::mutex::scoped_lock lock(updatingPortsLock);
            BULKIO::UsesPortStatisticsSequence_var recStat = new BULKIO::UsesPortStatisticsSequence();
            recStat->length(outConnections.size());
            for (unsigned int i = 0; i < outConnections.size(); i++) {
                recStat[i].connectionId = CORBA::string_dup(outConnections[i].second.c_str());
                recStat[i].statistics = stats[outConnections[i].second].retrieve();
            }
            return recStat._retn();
        };

        BULKIO::PortUsageType state()
        {
            boost::mutex::scoped_lock lock(updatingPortsLock);
            if (outConnections.size() > 0) {
                return BULKIO::ACTIVE;
            } else {
                return BULKIO::IDLE;
            }

            return BULKIO::BUSY;
        };
        
        void enableStats(bool enable)
        {
            for (unsigned int i = 0; i < outConnections.size(); i++) {
                stats[outConnections[i].second].setEnabled(enable);
            }
        };


        ExtendedCF::UsesConnectionSequence * connections() 
        {
            boost::mutex::scoped_lock lock(updatingPortsLock);   // don't want to process while command information is coming in
            if (recConnectionsRefresh) {
                recConnections.length(outConnections.size());
                for (unsigned int i = 0; i < outConnections.size(); i++) {
                    recConnections[i].connectionId = CORBA::string_dup(outConnections[i].second.c_str());
                    recConnections[i].port = CORBA::Object::_duplicate(outConnections[i].first);
                }
                recConnectionsRefresh = false;
            }
            ExtendedCF::UsesConnectionSequence_var retVal = new ExtendedCF::UsesConnectionSequence(recConnections);
            // NOTE: You must delete the object that this function returns!
            return retVal._retn();
        };

        void connectPort(CORBA::Object_ptr connection, const char* connectionId)
        {
            boost::mutex::scoped_lock lock(updatingPortsLock);   // don't want to process while command information is coming in
            BULKIO::dataDouble_var port = BULKIO::dataDouble::_narrow(connection);
            outConnections.push_back(std::make_pair(port, connectionId));
            active = true;
            recConnectionsRefresh = true;
            refreshSRI = true;
        };

        void disconnectPort(const char* connectionId)
        {
            boost::mutex::scoped_lock lock(updatingPortsLock);   // don't want to process while command information is coming in
            for (unsigned int i = 0; i < outConnections.size(); i++) {
                if (outConnections[i].second == connectionId) {
                    outConnections.erase(outConnections.begin() + i);
                    break;
                }
            }

            if (outConnections.size() == 0) {
                active = false;
            }
            recConnectionsRefresh = true;
        };

        std::vector< std::pair<BULKIO::dataDouble_var, std::string> > _getConnections()
        {
            return outConnections;
        };
        std::map<std::string, BULKIO::StreamSRI> currentSRIs;

    protected:
        DataConverter_i *parent;
        std::vector < std::pair<BULKIO::dataDouble_var, std::string> > outConnections;
        ExtendedCF::UsesConnectionSequence recConnections;
        bool recConnectionsRefresh;
        std::map<std::string, linkStatistics> stats;
};


// ----------------------------------------------------------------------------------------
// BULKIO_dataFloat_Out_i declaration
// ----------------------------------------------------------------------------------------
class BULKIO_dataFloat_Out_i : public Port_Uses_base_impl, public virtual POA_BULKIO::UsesPortStatisticsProvider
{
    ENABLE_LOGGING
    public:
        BULKIO_dataFloat_Out_i(std::string port_name, DataConverter_base *_parent);
        ~BULKIO_dataFloat_Out_i();

        void pushSRI(const BULKIO::StreamSRI& H);
        
        /*
         * pushPacket
         *     description: push data out of the port
         *
         *  data: structure containing the payload to send out
         *  T: constant of type BULKIO::PrecisionUTCTime containing the timestamp for the outgoing data.
         *    tcmode: timecode mode
         *    tcstatus: timecode status 
         *    toff: fractional sample offset
         *    twsec: J1970 GMT 
         *    tfsec: fractional seconds: 0.0 to 1.0
         *  EOS: end-of-stream flag
         *  streamID: stream identifier
         */
        template <typename ALLOCATOR>
        void pushPacket(std::vector<CORBA::Float, ALLOCATOR>& data, BULKIO::PrecisionUTCTime& T, bool EOS, const std::string& streamID) {
            if (refreshSRI) {
                if (currentSRIs.find(streamID) == currentSRIs.end()) {
                    BULKIO::StreamSRI sri;
                    sri.hversion = 1;
                    sri.xstart = 0.0;
                    sri.xdelta = 1.0;
                    sri.xunits = BULKIO::UNITS_TIME;
                    sri.subsize = 0;
                    sri.ystart = 0.0;
                    sri.ydelta = 0.0;
                    sri.yunits = BULKIO::UNITS_NONE;
                    sri.mode = 0;
                    sri.blocking = false;
                    sri.streamID = streamID.c_str();
                    currentSRIs[streamID] = sri;
                }
                pushSRI(currentSRIs[streamID]);
            }
            boost::mutex::scoped_lock lock(updatingPortsLock);   // don't want to process while command information is coming in
            // Magic is below, make a new sequence using the data from the Iterator
            // as the data for the sequence.  The 'false' at the end is whether or not
            // CORBA is allowed to delete the buffer when the sequence is destroyed.
            PortTypes::FloatSequence seq = PortTypes::FloatSequence(data.size(), data.size(), &(data[0]), false);
            if (active) {
                std::vector < std::pair < BULKIO::dataFloat_var, std::string > >::iterator port;
                for (port = outConnections.begin(); port != outConnections.end(); port++) {
                    try {
                        ((*port).first)->pushPacket(seq, T, EOS, streamID.c_str());
                        stats[(*port).second].update(data.size(), 0, EOS, streamID);
                    } catch(...) {
                        LOG_ERROR(BULKIO_dataFloat_Out_i, "Call to pushPacket by BULKIO_dataFloat_Out_i failed");
                    }
                }
            }
            // for end of stream,  remove old sri
            try {
                if ( EOS ) currentSRIs.erase(streamID);
            }
            catch(...){
            }

        };
        class linkStatistics
        {
            public:
                struct statPoint {
                    unsigned int elements;
                    float queueSize;
                    double secs;
                    double usecs;
                };
                
                linkStatistics() {
                    bitSize = sizeof(CORBA::Float) * 8.0;
                    historyWindow = 10;
                    activeStreamIDs.resize(0);
                    receivedStatistics_idx = 0;
                    receivedStatistics.resize(historyWindow);
                    runningStats.elementsPerSecond = -1.0;
                    runningStats.bitsPerSecond = -1.0;
                    runningStats.callsPerSecond = -1.0;
                    runningStats.averageQueueDepth = -1.0;
                    runningStats.streamIDs.length(0);
                    runningStats.timeSinceLastCall = -1;
                    enabled = true;
                };

                void setEnabled(bool enableStats) {
                    enabled = enableStats;
                }

                void update(unsigned int elementsReceived, float queueSize, bool EOS, std::string streamID) {
                    if (!enabled) {
                        return;
                    }
                    struct timeval tv;
                    struct timezone tz;
                    gettimeofday(&tv, &tz);
                    receivedStatistics[receivedStatistics_idx].elements = elementsReceived;
                    receivedStatistics[receivedStatistics_idx].queueSize = queueSize;
                    receivedStatistics[receivedStatistics_idx].secs = tv.tv_sec;
                    receivedStatistics[receivedStatistics_idx++].usecs = tv.tv_usec;
                    receivedStatistics_idx = receivedStatistics_idx % historyWindow;
                    if (!EOS) {
                        std::list<std::string>::iterator p = activeStreamIDs.begin();
                        bool foundStreamID = false;
                        while (p != activeStreamIDs.end()) {
                            if (*p == streamID) {
                                foundStreamID = true;
                                break;
                            }
                            p++;
                        }
                        if (!foundStreamID) {
                            activeStreamIDs.push_back(streamID);
                        }
                    } else {
                        std::list<std::string>::iterator p = activeStreamIDs.begin();
                        while (p != activeStreamIDs.end()) {
                            if (*p == streamID) {
                                activeStreamIDs.erase(p);
                                break;
                            }
                            p++;
                        }
                    }
                };

                BULKIO::PortStatistics retrieve() {
                    if (!enabled) {
                        return runningStats;
                    }
                    struct timeval tv;
                    struct timezone tz;
                    gettimeofday(&tv, &tz);

                    int idx = (receivedStatistics_idx == 0) ? (historyWindow - 1) : (receivedStatistics_idx - 1);
                    double front_sec = receivedStatistics[idx].secs;
                    double front_usec = receivedStatistics[idx].usecs;
                    double secDiff = tv.tv_sec - receivedStatistics[receivedStatistics_idx].secs;
                    double usecDiff = (tv.tv_usec - receivedStatistics[receivedStatistics_idx].usecs) / ((double)1e6);

                    double totalTime = secDiff + usecDiff;
                    double totalData = 0;
                    float queueSize = 0;
                    int startIdx = (receivedStatistics_idx + 1) % historyWindow;
                    for (int i = startIdx; i != receivedStatistics_idx; ) {
                        totalData += receivedStatistics[i].elements;
                        queueSize += receivedStatistics[i].queueSize;
                        i = (i + 1) % historyWindow;
                    }
                    runningStats.bitsPerSecond = ((totalData * bitSize) / totalTime);
                    runningStats.elementsPerSecond = (totalData / totalTime);
                    runningStats.averageQueueDepth = (queueSize / historyWindow);
                    runningStats.callsPerSecond = (double(historyWindow - 1) / totalTime);
                    runningStats.timeSinceLastCall = (((double)tv.tv_sec) - front_sec) + (((double)tv.tv_usec - front_usec) / ((double)1e6));
                    unsigned int streamIDsize = activeStreamIDs.size();
                    std::list< std::string >::iterator p = activeStreamIDs.begin();
                    runningStats.streamIDs.length(streamIDsize);
                    for (unsigned int i = 0; i < streamIDsize; i++) {
                        if (p == activeStreamIDs.end()) {
                            break;
                        }
                        runningStats.streamIDs[i] = CORBA::string_dup((*p).c_str());
                        p++;
                    }
                    return runningStats;
                };

            protected:
                bool enabled;
                double bitSize;
                BULKIO::PortStatistics runningStats;
                std::vector<statPoint> receivedStatistics;
                std::list< std::string > activeStreamIDs;
                unsigned long historyWindow;
                int receivedStatistics_idx;
        };

        BULKIO::UsesPortStatisticsSequence * statistics()
        {
            boost::mutex::scoped_lock lock(updatingPortsLock);
            BULKIO::UsesPortStatisticsSequence_var recStat = new BULKIO::UsesPortStatisticsSequence();
            recStat->length(outConnections.size());
            for (unsigned int i = 0; i < outConnections.size(); i++) {
                recStat[i].connectionId = CORBA::string_dup(outConnections[i].second.c_str());
                recStat[i].statistics = stats[outConnections[i].second].retrieve();
            }
            return recStat._retn();
        };

        BULKIO::PortUsageType state()
        {
            boost::mutex::scoped_lock lock(updatingPortsLock);
            if (outConnections.size() > 0) {
                return BULKIO::ACTIVE;
            } else {
                return BULKIO::IDLE;
            }

            return BULKIO::BUSY;
        };
        
        void enableStats(bool enable)
        {
            for (unsigned int i = 0; i < outConnections.size(); i++) {
                stats[outConnections[i].second].setEnabled(enable);
            }
        };


        ExtendedCF::UsesConnectionSequence * connections() 
        {
            boost::mutex::scoped_lock lock(updatingPortsLock);   // don't want to process while command information is coming in
            if (recConnectionsRefresh) {
                recConnections.length(outConnections.size());
                for (unsigned int i = 0; i < outConnections.size(); i++) {
                    recConnections[i].connectionId = CORBA::string_dup(outConnections[i].second.c_str());
                    recConnections[i].port = CORBA::Object::_duplicate(outConnections[i].first);
                }
                recConnectionsRefresh = false;
            }
            ExtendedCF::UsesConnectionSequence_var retVal = new ExtendedCF::UsesConnectionSequence(recConnections);
            // NOTE: You must delete the object that this function returns!
            return retVal._retn();
        };

        void connectPort(CORBA::Object_ptr connection, const char* connectionId)
        {
            boost::mutex::scoped_lock lock(updatingPortsLock);   // don't want to process while command information is coming in
            BULKIO::dataFloat_var port = BULKIO::dataFloat::_narrow(connection);
            outConnections.push_back(std::make_pair(port, connectionId));
            active = true;
            recConnectionsRefresh = true;
            refreshSRI = true;
        };

        void disconnectPort(const char* connectionId)
        {
            boost::mutex::scoped_lock lock(updatingPortsLock);   // don't want to process while command information is coming in
            for (unsigned int i = 0; i < outConnections.size(); i++) {
                if (outConnections[i].second == connectionId) {
                    outConnections.erase(outConnections.begin() + i);
                    break;
                }
            }

            if (outConnections.size() == 0) {
                active = false;
            }
            recConnectionsRefresh = true;
        };

        std::vector< std::pair<BULKIO::dataFloat_var, std::string> > _getConnections()
        {
            return outConnections;
        };
        std::map<std::string, BULKIO::StreamSRI> currentSRIs;

    protected:
        DataConverter_i *parent;
        std::vector < std::pair<BULKIO::dataFloat_var, std::string> > outConnections;
        ExtendedCF::UsesConnectionSequence recConnections;
        bool recConnectionsRefresh;
        std::map<std::string, linkStatistics> stats;
};


// ----------------------------------------------------------------------------------------
// BULKIO_dataLong_Out_i declaration
// ----------------------------------------------------------------------------------------
class BULKIO_dataLong_Out_i : public Port_Uses_base_impl, public virtual POA_BULKIO::UsesPortStatisticsProvider
{
    ENABLE_LOGGING
    public:
        BULKIO_dataLong_Out_i(std::string port_name, DataConverter_base *_parent);
        ~BULKIO_dataLong_Out_i();

        void pushSRI(const BULKIO::StreamSRI& H);
        
        /*
         * pushPacket
         *     description: push data out of the port
         *
         *  data: structure containing the payload to send out
         *  T: constant of type BULKIO::PrecisionUTCTime containing the timestamp for the outgoing data.
         *    tcmode: timecode mode
         *    tcstatus: timecode status 
         *    toff: fractional sample offset
         *    twsec: J1970 GMT 
         *    tfsec: fractional seconds: 0.0 to 1.0
         *  EOS: end-of-stream flag
         *  streamID: stream identifier
         */
        template <typename ALLOCATOR>
        void pushPacket(std::vector<CORBA::Long, ALLOCATOR>& data, BULKIO::PrecisionUTCTime& T, bool EOS, const std::string& streamID) {
            if (refreshSRI) {
                if (currentSRIs.find(streamID) == currentSRIs.end()) {
                    BULKIO::StreamSRI sri;
                    sri.hversion = 1;
                    sri.xstart = 0.0;
                    sri.xdelta = 1.0;
                    sri.xunits = BULKIO::UNITS_TIME;
                    sri.subsize = 0;
                    sri.ystart = 0.0;
                    sri.ydelta = 0.0;
                    sri.yunits = BULKIO::UNITS_NONE;
                    sri.mode = 0;
                    sri.blocking = false;
                    sri.streamID = streamID.c_str();
                    currentSRIs[streamID] = sri;
                }
                pushSRI(currentSRIs[streamID]);
            }
            boost::mutex::scoped_lock lock(updatingPortsLock);   // don't want to process while command information is coming in
            // Magic is below, make a new sequence using the data from the Iterator
            // as the data for the sequence.  The 'false' at the end is whether or not
            // CORBA is allowed to delete the buffer when the sequence is destroyed.
            PortTypes::LongSequence seq = PortTypes::LongSequence(data.size(), data.size(), &(data[0]), false);
            if (active) {
                std::vector < std::pair < BULKIO::dataLong_var, std::string > >::iterator port;
                for (port = outConnections.begin(); port != outConnections.end(); port++) {
                    try {
                        ((*port).first)->pushPacket(seq, T, EOS, streamID.c_str());
                        stats[(*port).second].update(data.size(), 0, EOS, streamID);
                    } catch(...) {
                        LOG_ERROR(BULKIO_dataLong_Out_i, "Call to pushPacket by BULKIO_dataLong_Out_i failed");
                    }
                }
            }
            // for end of stream,  remove old sri
            try {
                if ( EOS ) currentSRIs.erase(streamID);
            }
            catch(...){
            }

        };
        class linkStatistics
        {
            public:
                struct statPoint {
                    unsigned int elements;
                    float queueSize;
                    double secs;
                    double usecs;
                };
                
                linkStatistics() {
                    bitSize = sizeof(CORBA::Long) * 8.0;
                    historyWindow = 10;
                    activeStreamIDs.resize(0);
                    receivedStatistics_idx = 0;
                    receivedStatistics.resize(historyWindow);
                    runningStats.elementsPerSecond = -1.0;
                    runningStats.bitsPerSecond = -1.0;
                    runningStats.callsPerSecond = -1.0;
                    runningStats.averageQueueDepth = -1.0;
                    runningStats.streamIDs.length(0);
                    runningStats.timeSinceLastCall = -1;
                    enabled = true;
                };

                void setEnabled(bool enableStats) {
                    enabled = enableStats;
                }

                void update(unsigned int elementsReceived, float queueSize, bool EOS, std::string streamID) {
                    if (!enabled) {
                        return;
                    }
                    struct timeval tv;
                    struct timezone tz;
                    gettimeofday(&tv, &tz);
                    receivedStatistics[receivedStatistics_idx].elements = elementsReceived;
                    receivedStatistics[receivedStatistics_idx].queueSize = queueSize;
                    receivedStatistics[receivedStatistics_idx].secs = tv.tv_sec;
                    receivedStatistics[receivedStatistics_idx++].usecs = tv.tv_usec;
                    receivedStatistics_idx = receivedStatistics_idx % historyWindow;
                    if (!EOS) {
                        std::list<std::string>::iterator p = activeStreamIDs.begin();
                        bool foundStreamID = false;
                        while (p != activeStreamIDs.end()) {
                            if (*p == streamID) {
                                foundStreamID = true;
                                break;
                            }
                            p++;
                        }
                        if (!foundStreamID) {
                            activeStreamIDs.push_back(streamID);
                        }
                    } else {
                        std::list<std::string>::iterator p = activeStreamIDs.begin();
                        while (p != activeStreamIDs.end()) {
                            if (*p == streamID) {
                                activeStreamIDs.erase(p);
                                break;
                            }
                            p++;
                        }
                    }
                };

                BULKIO::PortStatistics retrieve() {
                    if (!enabled) {
                        return runningStats;
                    }
                    struct timeval tv;
                    struct timezone tz;
                    gettimeofday(&tv, &tz);

                    int idx = (receivedStatistics_idx == 0) ? (historyWindow - 1) : (receivedStatistics_idx - 1);
                    double front_sec = receivedStatistics[idx].secs;
                    double front_usec = receivedStatistics[idx].usecs;
                    double secDiff = tv.tv_sec - receivedStatistics[receivedStatistics_idx].secs;
                    double usecDiff = (tv.tv_usec - receivedStatistics[receivedStatistics_idx].usecs) / ((double)1e6);

                    double totalTime = secDiff + usecDiff;
                    double totalData = 0;
                    float queueSize = 0;
                    int startIdx = (receivedStatistics_idx + 1) % historyWindow;
                    for (int i = startIdx; i != receivedStatistics_idx; ) {
                        totalData += receivedStatistics[i].elements;
                        queueSize += receivedStatistics[i].queueSize;
                        i = (i + 1) % historyWindow;
                    }
                    runningStats.bitsPerSecond = ((totalData * bitSize) / totalTime);
                    runningStats.elementsPerSecond = (totalData / totalTime);
                    runningStats.averageQueueDepth = (queueSize / historyWindow);
                    runningStats.callsPerSecond = (double(historyWindow - 1) / totalTime);
                    runningStats.timeSinceLastCall = (((double)tv.tv_sec) - front_sec) + (((double)tv.tv_usec - front_usec) / ((double)1e6));
                    unsigned int streamIDsize = activeStreamIDs.size();
                    std::list< std::string >::iterator p = activeStreamIDs.begin();
                    runningStats.streamIDs.length(streamIDsize);
                    for (unsigned int i = 0; i < streamIDsize; i++) {
                        if (p == activeStreamIDs.end()) {
                            break;
                        }
                        runningStats.streamIDs[i] = CORBA::string_dup((*p).c_str());
                        p++;
                    }
                    return runningStats;
                };

            protected:
                bool enabled;
                double bitSize;
                BULKIO::PortStatistics runningStats;
                std::vector<statPoint> receivedStatistics;
                std::list< std::string > activeStreamIDs;
                unsigned long historyWindow;
                int receivedStatistics_idx;
        };

        BULKIO::UsesPortStatisticsSequence * statistics()
        {
            boost::mutex::scoped_lock lock(updatingPortsLock);
            BULKIO::UsesPortStatisticsSequence_var recStat = new BULKIO::UsesPortStatisticsSequence();
            recStat->length(outConnections.size());
            for (unsigned int i = 0; i < outConnections.size(); i++) {
                recStat[i].connectionId = CORBA::string_dup(outConnections[i].second.c_str());
                recStat[i].statistics = stats[outConnections[i].second].retrieve();
            }
            return recStat._retn();
        };

        BULKIO::PortUsageType state()
        {
            boost::mutex::scoped_lock lock(updatingPortsLock);
            if (outConnections.size() > 0) {
                return BULKIO::ACTIVE;
            } else {
                return BULKIO::IDLE;
            }

            return BULKIO::BUSY;
        };
        
        void enableStats(bool enable)
        {
            for (unsigned int i = 0; i < outConnections.size(); i++) {
                stats[outConnections[i].second].setEnabled(enable);
            }
        };


        ExtendedCF::UsesConnectionSequence * connections() 
        {
            boost::mutex::scoped_lock lock(updatingPortsLock);   // don't want to process while command information is coming in
            if (recConnectionsRefresh) {
                recConnections.length(outConnections.size());
                for (unsigned int i = 0; i < outConnections.size(); i++) {
                    recConnections[i].connectionId = CORBA::string_dup(outConnections[i].second.c_str());
                    recConnections[i].port = CORBA::Object::_duplicate(outConnections[i].first);
                }
                recConnectionsRefresh = false;
            }
            ExtendedCF::UsesConnectionSequence_var retVal = new ExtendedCF::UsesConnectionSequence(recConnections);
            // NOTE: You must delete the object that this function returns!
            return retVal._retn();
        };

        void connectPort(CORBA::Object_ptr connection, const char* connectionId)
        {
            boost::mutex::scoped_lock lock(updatingPortsLock);   // don't want to process while command information is coming in
            BULKIO::dataLong_var port = BULKIO::dataLong::_narrow(connection);
            outConnections.push_back(std::make_pair(port, connectionId));
            active = true;
            recConnectionsRefresh = true;
            refreshSRI = true;
        };

        void disconnectPort(const char* connectionId)
        {
            boost::mutex::scoped_lock lock(updatingPortsLock);   // don't want to process while command information is coming in
            for (unsigned int i = 0; i < outConnections.size(); i++) {
                if (outConnections[i].second == connectionId) {
                    outConnections.erase(outConnections.begin() + i);
                    break;
                }
            }

            if (outConnections.size() == 0) {
                active = false;
            }
            recConnectionsRefresh = true;
        };

        std::vector< std::pair<BULKIO::dataLong_var, std::string> > _getConnections()
        {
            return outConnections;
        };
        std::map<std::string, BULKIO::StreamSRI> currentSRIs;

    protected:
        DataConverter_i *parent;
        std::vector < std::pair<BULKIO::dataLong_var, std::string> > outConnections;
        ExtendedCF::UsesConnectionSequence recConnections;
        bool recConnectionsRefresh;
        std::map<std::string, linkStatistics> stats;
};


// ----------------------------------------------------------------------------------------
// BULKIO_dataOctet_Out_i declaration
// ----------------------------------------------------------------------------------------
class BULKIO_dataOctet_Out_i : public Port_Uses_base_impl, public virtual POA_BULKIO::UsesPortStatisticsProvider
{
    ENABLE_LOGGING
    public:
        BULKIO_dataOctet_Out_i(std::string port_name, DataConverter_base *_parent);
        ~BULKIO_dataOctet_Out_i();

        void pushSRI(const BULKIO::StreamSRI& H);
        
        /*
         * pushPacket
         *     description: push data out of the port
         *
         *  data: structure containing the payload to send out
         *  T: constant of type BULKIO::PrecisionUTCTime containing the timestamp for the outgoing data.
         *    tcmode: timecode mode
         *    tcstatus: timecode status 
         *    toff: fractional sample offset
         *    twsec: J1970 GMT 
         *    tfsec: fractional seconds: 0.0 to 1.0
         *  EOS: end-of-stream flag
         *  streamID: stream identifier
         */
        template <typename ALLOCATOR>
        void pushPacket(std::vector<unsigned char, ALLOCATOR>& data, BULKIO::PrecisionUTCTime& T, bool EOS, const std::string& streamID) {
            if (refreshSRI) {
                if (currentSRIs.find(streamID) == currentSRIs.end()) {
                    BULKIO::StreamSRI sri;
                    sri.hversion = 1;
                    sri.xstart = 0.0;
                    sri.xdelta = 1.0;
                    sri.xunits = BULKIO::UNITS_TIME;
                    sri.subsize = 0;
                    sri.ystart = 0.0;
                    sri.ydelta = 0.0;
                    sri.yunits = BULKIO::UNITS_NONE;
                    sri.mode = 0;
                    sri.blocking = false;
                    sri.streamID = streamID.c_str();
                    currentSRIs[streamID] = sri;
                }
                pushSRI(currentSRIs[streamID]);
            }
            boost::mutex::scoped_lock lock(updatingPortsLock);   // don't want to process while command information is coming in
            // Magic is below, make a new sequence using the data from the Iterator
            // as the data for the sequence.  The 'false' at the end is whether or not
            // CORBA is allowed to delete the buffer when the sequence is destroyed.
            CF::OctetSequence seq = CF::OctetSequence(data.size(), data.size(), &(data[0]), false);
            if (active) {
                std::vector < std::pair < BULKIO::dataOctet_var, std::string > >::iterator port;
                for (port = outConnections.begin(); port != outConnections.end(); port++) {
                    try {
                        ((*port).first)->pushPacket(seq, T, EOS, streamID.c_str());
                        stats[(*port).second].update(data.size(), 0, EOS, streamID);
                    } catch(...) {
                        LOG_ERROR(BULKIO_dataOctet_Out_i, "Call to pushPacket by BULKIO_dataOctet_Out_i failed");
                    }
                }
            }
            // for end of stream,  remove old sri
            try {
                if ( EOS ) currentSRIs.erase(streamID);
            }
            catch(...){
            }

        };
        class linkStatistics
        {
            public:
                struct statPoint {
                    unsigned int elements;
                    float queueSize;
                    double secs;
                    double usecs;
                };
                
                linkStatistics() {
                    bitSize = sizeof(unsigned char) * 8.0;
                    historyWindow = 10;
                    activeStreamIDs.resize(0);
                    receivedStatistics_idx = 0;
                    receivedStatistics.resize(historyWindow);
                    runningStats.elementsPerSecond = -1.0;
                    runningStats.bitsPerSecond = -1.0;
                    runningStats.callsPerSecond = -1.0;
                    runningStats.averageQueueDepth = -1.0;
                    runningStats.streamIDs.length(0);
                    runningStats.timeSinceLastCall = -1;
                    enabled = true;
                };

                void setEnabled(bool enableStats) {
                    enabled = enableStats;
                }

                void update(unsigned int elementsReceived, float queueSize, bool EOS, std::string streamID) {
                    if (!enabled) {
                        return;
                    }
                    struct timeval tv;
                    struct timezone tz;
                    gettimeofday(&tv, &tz);
                    receivedStatistics[receivedStatistics_idx].elements = elementsReceived;
                    receivedStatistics[receivedStatistics_idx].queueSize = queueSize;
                    receivedStatistics[receivedStatistics_idx].secs = tv.tv_sec;
                    receivedStatistics[receivedStatistics_idx++].usecs = tv.tv_usec;
                    receivedStatistics_idx = receivedStatistics_idx % historyWindow;
                    if (!EOS) {
                        std::list<std::string>::iterator p = activeStreamIDs.begin();
                        bool foundStreamID = false;
                        while (p != activeStreamIDs.end()) {
                            if (*p == streamID) {
                                foundStreamID = true;
                                break;
                            }
                            p++;
                        }
                        if (!foundStreamID) {
                            activeStreamIDs.push_back(streamID);
                        }
                    } else {
                        std::list<std::string>::iterator p = activeStreamIDs.begin();
                        while (p != activeStreamIDs.end()) {
                            if (*p == streamID) {
                                activeStreamIDs.erase(p);
                                break;
                            }
                            p++;
                        }
                    }
                };

                BULKIO::PortStatistics retrieve() {
                    if (!enabled) {
                        return runningStats;
                    }
                    struct timeval tv;
                    struct timezone tz;
                    gettimeofday(&tv, &tz);

                    int idx = (receivedStatistics_idx == 0) ? (historyWindow - 1) : (receivedStatistics_idx - 1);
                    double front_sec = receivedStatistics[idx].secs;
                    double front_usec = receivedStatistics[idx].usecs;
                    double secDiff = tv.tv_sec - receivedStatistics[receivedStatistics_idx].secs;
                    double usecDiff = (tv.tv_usec - receivedStatistics[receivedStatistics_idx].usecs) / ((double)1e6);

                    double totalTime = secDiff + usecDiff;
                    double totalData = 0;
                    float queueSize = 0;
                    int startIdx = (receivedStatistics_idx + 1) % historyWindow;
                    for (int i = startIdx; i != receivedStatistics_idx; ) {
                        totalData += receivedStatistics[i].elements;
                        queueSize += receivedStatistics[i].queueSize;
                        i = (i + 1) % historyWindow;
                    }
                    runningStats.bitsPerSecond = ((totalData * bitSize) / totalTime);
                    runningStats.elementsPerSecond = (totalData / totalTime);
                    runningStats.averageQueueDepth = (queueSize / historyWindow);
                    runningStats.callsPerSecond = (double(historyWindow - 1) / totalTime);
                    runningStats.timeSinceLastCall = (((double)tv.tv_sec) - front_sec) + (((double)tv.tv_usec - front_usec) / ((double)1e6));
                    unsigned int streamIDsize = activeStreamIDs.size();
                    std::list< std::string >::iterator p = activeStreamIDs.begin();
                    runningStats.streamIDs.length(streamIDsize);
                    for (unsigned int i = 0; i < streamIDsize; i++) {
                        if (p == activeStreamIDs.end()) {
                            break;
                        }
                        runningStats.streamIDs[i] = CORBA::string_dup((*p).c_str());
                        p++;
                    }
                    return runningStats;
                };

            protected:
                bool enabled;
                double bitSize;
                BULKIO::PortStatistics runningStats;
                std::vector<statPoint> receivedStatistics;
                std::list< std::string > activeStreamIDs;
                unsigned long historyWindow;
                int receivedStatistics_idx;
        };

        BULKIO::UsesPortStatisticsSequence * statistics()
        {
            boost::mutex::scoped_lock lock(updatingPortsLock);
            BULKIO::UsesPortStatisticsSequence_var recStat = new BULKIO::UsesPortStatisticsSequence();
            recStat->length(outConnections.size());
            for (unsigned int i = 0; i < outConnections.size(); i++) {
                recStat[i].connectionId = CORBA::string_dup(outConnections[i].second.c_str());
                recStat[i].statistics = stats[outConnections[i].second].retrieve();
            }
            return recStat._retn();
        };

        BULKIO::PortUsageType state()
        {
            boost::mutex::scoped_lock lock(updatingPortsLock);
            if (outConnections.size() > 0) {
                return BULKIO::ACTIVE;
            } else {
                return BULKIO::IDLE;
            }

            return BULKIO::BUSY;
        };
        
        void enableStats(bool enable)
        {
            for (unsigned int i = 0; i < outConnections.size(); i++) {
                stats[outConnections[i].second].setEnabled(enable);
            }
        };


        ExtendedCF::UsesConnectionSequence * connections() 
        {
            boost::mutex::scoped_lock lock(updatingPortsLock);   // don't want to process while command information is coming in
            if (recConnectionsRefresh) {
                recConnections.length(outConnections.size());
                for (unsigned int i = 0; i < outConnections.size(); i++) {
                    recConnections[i].connectionId = CORBA::string_dup(outConnections[i].second.c_str());
                    recConnections[i].port = CORBA::Object::_duplicate(outConnections[i].first);
                }
                recConnectionsRefresh = false;
            }
            ExtendedCF::UsesConnectionSequence_var retVal = new ExtendedCF::UsesConnectionSequence(recConnections);
            // NOTE: You must delete the object that this function returns!
            return retVal._retn();
        };

        void connectPort(CORBA::Object_ptr connection, const char* connectionId)
        {
            boost::mutex::scoped_lock lock(updatingPortsLock);   // don't want to process while command information is coming in
            BULKIO::dataOctet_var port = BULKIO::dataOctet::_narrow(connection);
            outConnections.push_back(std::make_pair(port, connectionId));
            active = true;
            recConnectionsRefresh = true;
            refreshSRI = true;
        };

        void disconnectPort(const char* connectionId)
        {
            boost::mutex::scoped_lock lock(updatingPortsLock);   // don't want to process while command information is coming in
            for (unsigned int i = 0; i < outConnections.size(); i++) {
                if (outConnections[i].second == connectionId) {
                    outConnections.erase(outConnections.begin() + i);
                    break;
                }
            }

            if (outConnections.size() == 0) {
                active = false;
            }
            recConnectionsRefresh = true;
        };

        std::vector< std::pair<BULKIO::dataOctet_var, std::string> > _getConnections()
        {
            return outConnections;
        };
        std::map<std::string, BULKIO::StreamSRI> currentSRIs;

    protected:
        DataConverter_i *parent;
        std::vector < std::pair<BULKIO::dataOctet_var, std::string> > outConnections;
        ExtendedCF::UsesConnectionSequence recConnections;
        bool recConnectionsRefresh;
        std::map<std::string, linkStatistics> stats;
};


class queueSemaphore
{
    private:
        unsigned int maxValue;
        unsigned int currValue;
        boost::mutex mutex;
        boost::condition_variable condition;

    public:
        queueSemaphore(unsigned int initialMaxValue):mutex(),condition() {
        	maxValue = initialMaxValue;
        }

        void setMaxValue(unsigned int newMaxValue) {
            boost::unique_lock<boost::mutex> lock(mutex);
            maxValue = newMaxValue;
        }

        unsigned int getMaxValue(void) {
			boost::unique_lock<boost::mutex> lock(mutex);
			return maxValue;
		}

        void setCurrValue(unsigned int newValue) {
        	boost::unique_lock<boost::mutex> lock(mutex);
        	if (newValue < maxValue) {
        		unsigned int oldValue = currValue;
        		currValue = newValue;

        		if (oldValue > newValue) {
        			condition.notify_one();
        		}
        	}
        }

        void incr() {
            boost::unique_lock<boost::mutex> lock(mutex);
            while (currValue >= maxValue) {
                condition.wait(lock);
            }
            ++currValue;
        }

        void decr() {
            boost::unique_lock<boost::mutex> lock(mutex);
            if (currValue > 0) {
            	--currValue;
            }
            condition.notify_one();
        }
};        



// ----------------------------------------------------------------------------------------
// BULKIO_dataUshort_In_i declaration
// ----------------------------------------------------------------------------------------
class BULKIO_dataUshort_In_i : public POA_BULKIO::dataUshort, public Port_Provides_base_impl
{
    public:
        BULKIO_dataUshort_In_i(std::string port_name, DataConverter_base *_parent);
        ~BULKIO_dataUshort_In_i();

        void pushSRI(const BULKIO::StreamSRI& H);
        void pushPacket(const PortTypes::UshortSequence& data, const BULKIO::PrecisionUTCTime& T, ::CORBA::Boolean EOS, const char* streamID);

        BULKIO::PortUsageType state();
        BULKIO::PortStatistics* statistics();
        BULKIO::StreamSRISequence* activeSRIs();
        int getCurrentQueueDepth();
        int getMaxQueueDepth();
        void setMaxQueueDepth(int newDepth);

        class linkStatistics
        {
            public:
                struct statPoint {
                    unsigned int elements;
                    float queueSize;
                    double secs;
                    double usecs;
                };

                linkStatistics() {
                    bitSize = sizeof(CORBA::UShort) * 8.0;
                    historyWindow = 10;
                    receivedStatistics_idx = 0;
                    receivedStatistics.resize(historyWindow);
                    activeStreamIDs.resize(0);
                    runningStats.elementsPerSecond = -1.0;
                    runningStats.bitsPerSecond = -1.0;
                    runningStats.callsPerSecond = -1.0;
                    runningStats.averageQueueDepth = -1.0;
                    runningStats.streamIDs.length(0);
                    runningStats.timeSinceLastCall = -1;
                    enabled = true;
                    flush_sec = 0;
                    flush_usec = 0;
                };

                ~linkStatistics() {
                }

                void setEnabled(bool enableStats) {
                    enabled = enableStats;
                }

                void update(unsigned int elementsReceived, float queueSize, bool EOS, std::string streamID, bool flush) {
                    if (!enabled) {
                        return;
                    }
                    struct timeval tv;
                    struct timezone tz;
                    gettimeofday(&tv, &tz);
                    receivedStatistics[receivedStatistics_idx].elements = elementsReceived;
                    receivedStatistics[receivedStatistics_idx].queueSize = queueSize;
                    receivedStatistics[receivedStatistics_idx].secs = tv.tv_sec;
                    receivedStatistics[receivedStatistics_idx++].usecs = tv.tv_usec;
                    receivedStatistics_idx = receivedStatistics_idx % historyWindow;
                    if (flush) {
                        flush_sec = tv.tv_sec;
                        flush_usec = tv.tv_usec;
                    }
                    if (!EOS) {
                        std::list<std::string>::iterator p = activeStreamIDs.begin();
                        bool foundStreamID = false;
                        while (p != activeStreamIDs.end()) {
                            if (*p == streamID) {
                                foundStreamID = true;
                                break;
                            }
                            p++;
                        }
                        if (!foundStreamID) {
                            activeStreamIDs.push_back(streamID);
                        }
                    } else {
                        std::list<std::string>::iterator p = activeStreamIDs.begin();
                        while (p != activeStreamIDs.end()) {
                            if (*p == streamID) {
                                activeStreamIDs.erase(p);
                                break;
                            }
                            p++;
                        }
                    }
                }

                BULKIO::PortStatistics retrieve() {
                    if (!enabled) {
                        return runningStats;
                    }
                    struct timeval tv;
                    struct timezone tz;
                    gettimeofday(&tv, &tz);

                    int idx = (receivedStatistics_idx == 0) ? (historyWindow - 1) : (receivedStatistics_idx - 1);
                    double front_sec = receivedStatistics[idx].secs;
                    double front_usec = receivedStatistics[idx].usecs;
                    double secDiff = tv.tv_sec - receivedStatistics[receivedStatistics_idx].secs;
                    double usecDiff = (tv.tv_usec - receivedStatistics[receivedStatistics_idx].usecs) / ((double)1e6);
                    double totalTime = secDiff + usecDiff;
                    double totalData = 0;
                    float queueSize = 0;
                    int startIdx = (receivedStatistics_idx + 1) % historyWindow;
                    for (int i = startIdx; i != receivedStatistics_idx; ) {
                        totalData += receivedStatistics[i].elements;
                        queueSize += receivedStatistics[i].queueSize;
                        i = (i + 1) % historyWindow;
                    }
                    runningStats.bitsPerSecond = ((totalData * bitSize) / totalTime);
                    runningStats.elementsPerSecond = (totalData / totalTime);
                    runningStats.averageQueueDepth = (queueSize / historyWindow);
                    runningStats.callsPerSecond = (double(historyWindow - 1) / totalTime);
                    runningStats.timeSinceLastCall = (((double)tv.tv_sec) - front_sec) + (((double)tv.tv_usec - front_usec) / ((double)1e6));
                    unsigned int streamIDsize = activeStreamIDs.size();
                    std::list< std::string >::iterator p = activeStreamIDs.begin();
                    runningStats.streamIDs.length(streamIDsize);
                    for (unsigned int i = 0; i < streamIDsize; i++) {
                        if (p == activeStreamIDs.end()) {
                            break;
                        }
                        runningStats.streamIDs[i] = CORBA::string_dup((*p).c_str());
                        p++;
                    }
                    if ((flush_sec != 0) && (flush_usec != 0)) {
                        double flushTotalTime = (((double)tv.tv_sec) - flush_sec) + (((double)tv.tv_usec - flush_usec) / ((double)1e6));
                        runningStats.keywords.length(1);
                        runningStats.keywords[0].id = CORBA::string_dup("timeSinceLastFlush");
                        runningStats.keywords[0].value <<= CORBA::Double(flushTotalTime);
                    }
                    return runningStats;
                }

            protected:
                bool enabled;
                double bitSize;
                BULKIO::PortStatistics runningStats;
                std::vector<statPoint> receivedStatistics;
                std::list< std::string > activeStreamIDs;
                unsigned long historyWindow;
                long receivedStatistics_idx;
                double flush_sec;
                double flush_usec;
        };
        
        void enableStats(bool enable) {
            stats.setEnabled(enable);
        };


        class dataTransfer
        {
            public:
                dataTransfer(const PortTypes::UshortSequence& data, const BULKIO::PrecisionUTCTime &_T, bool _EOS, const char* _streamID, BULKIO::StreamSRI &_H, bool _sriChanged, bool _inputQueueFlushed)
                {
                    int dataLength = data.length();

#ifdef EXPECTED_VECTOR_IMPL
                    std::_Vector_base<CORBA::UShort, _seqVector::seqVectorAllocator<CORBA::UShort> >::_Vector_impl *vectorPointer = (std::_Vector_base<CORBA::UShort, _seqVector::seqVectorAllocator<CORBA::UShort> >::_Vector_impl *) ((void*) & dataBuffer);
                    vectorPointer->_M_start = const_cast<PortTypes::UshortSequence*>(&data)->get_buffer(1);
                    vectorPointer->_M_finish = vectorPointer->_M_start + dataLength;
                    vectorPointer->_M_end_of_storage = vectorPointer->_M_finish;

#else
                    dataBuffer.resize(dataLength);
                    if (dataLength > 0) {
                        memcpy(&dataBuffer[0], &data[0], dataLength * sizeof(data[0]));
                    }

#endif
                    T = _T;
                    EOS = _EOS;
                    streamID = _streamID;
                    SRI = _H;
                    sriChanged = _sriChanged;
                    inputQueueFlushed = _inputQueueFlushed;
                };

#ifdef EXPECTED_VECTOR_IMPL
                std::vector< CORBA::UShort, _seqVector::seqVectorAllocator<CORBA::UShort> > dataBuffer;
#else
                std::vector<CORBA::UShort> dataBuffer;
#endif
                BULKIO::PrecisionUTCTime T;
                bool EOS;
                std::string streamID;
                BULKIO::StreamSRI SRI;
                bool sriChanged;
                bool inputQueueFlushed;
        };

        dataTransfer *getPacket(float timeout);
        void block();
        void unblock();

    protected:
        DataConverter_i *parent;
        std::deque<dataTransfer *> workQueue;
        std::map<std::string, std::pair<BULKIO::StreamSRI, bool> > currentHs;
        boost::mutex dataBufferLock;
        boost::mutex sriUpdateLock;
        omni_mutex dataAvailableMutex;
        omni_condition* dataAvailable;
        unsigned long secs, nsecs, timeout_secs, timeout_nsecs;
        bool breakBlock;
        bool blocking;
        queueSemaphore* queueSem;

        // statistics
        linkStatistics stats;

};



// ----------------------------------------------------------------------------------------
// BULKIO_dataShort_In_i declaration
// ----------------------------------------------------------------------------------------
class BULKIO_dataShort_In_i : public POA_BULKIO::dataShort, public Port_Provides_base_impl
{
    public:
        BULKIO_dataShort_In_i(std::string port_name, DataConverter_base *_parent);
        ~BULKIO_dataShort_In_i();

        void pushSRI(const BULKIO::StreamSRI& H);
        void pushPacket(const PortTypes::ShortSequence& data, const BULKIO::PrecisionUTCTime& T, ::CORBA::Boolean EOS, const char* streamID);

        BULKIO::PortUsageType state();
        BULKIO::PortStatistics* statistics();
        BULKIO::StreamSRISequence* activeSRIs();
        int getCurrentQueueDepth();
        int getMaxQueueDepth();
        void setMaxQueueDepth(int newDepth);

        class linkStatistics
        {
            public:
                struct statPoint {
                    unsigned int elements;
                    float queueSize;
                    double secs;
                    double usecs;
                };

                linkStatistics() {
                    bitSize = sizeof(CORBA::Short) * 8.0;
                    historyWindow = 10;
                    receivedStatistics_idx = 0;
                    receivedStatistics.resize(historyWindow);
                    activeStreamIDs.resize(0);
                    runningStats.elementsPerSecond = -1.0;
                    runningStats.bitsPerSecond = -1.0;
                    runningStats.callsPerSecond = -1.0;
                    runningStats.averageQueueDepth = -1.0;
                    runningStats.streamIDs.length(0);
                    runningStats.timeSinceLastCall = -1;
                    enabled = true;
                    flush_sec = 0;
                    flush_usec = 0;
                };

                ~linkStatistics() {
                }

                void setEnabled(bool enableStats) {
                    enabled = enableStats;
                }

                void update(unsigned int elementsReceived, float queueSize, bool EOS, std::string streamID, bool flush) {
                    if (!enabled) {
                        return;
                    }
                    struct timeval tv;
                    struct timezone tz;
                    gettimeofday(&tv, &tz);
                    receivedStatistics[receivedStatistics_idx].elements = elementsReceived;
                    receivedStatistics[receivedStatistics_idx].queueSize = queueSize;
                    receivedStatistics[receivedStatistics_idx].secs = tv.tv_sec;
                    receivedStatistics[receivedStatistics_idx++].usecs = tv.tv_usec;
                    receivedStatistics_idx = receivedStatistics_idx % historyWindow;
                    if (flush) {
                        flush_sec = tv.tv_sec;
                        flush_usec = tv.tv_usec;
                    }
                    if (!EOS) {
                        std::list<std::string>::iterator p = activeStreamIDs.begin();
                        bool foundStreamID = false;
                        while (p != activeStreamIDs.end()) {
                            if (*p == streamID) {
                                foundStreamID = true;
                                break;
                            }
                            p++;
                        }
                        if (!foundStreamID) {
                            activeStreamIDs.push_back(streamID);
                        }
                    } else {
                        std::list<std::string>::iterator p = activeStreamIDs.begin();
                        while (p != activeStreamIDs.end()) {
                            if (*p == streamID) {
                                activeStreamIDs.erase(p);
                                break;
                            }
                            p++;
                        }
                    }
                }

                BULKIO::PortStatistics retrieve() {
                    if (!enabled) {
                        return runningStats;
                    }
                    struct timeval tv;
                    struct timezone tz;
                    gettimeofday(&tv, &tz);

                    int idx = (receivedStatistics_idx == 0) ? (historyWindow - 1) : (receivedStatistics_idx - 1);
                    double front_sec = receivedStatistics[idx].secs;
                    double front_usec = receivedStatistics[idx].usecs;
                    double secDiff = tv.tv_sec - receivedStatistics[receivedStatistics_idx].secs;
                    double usecDiff = (tv.tv_usec - receivedStatistics[receivedStatistics_idx].usecs) / ((double)1e6);
                    double totalTime = secDiff + usecDiff;
                    double totalData = 0;
                    float queueSize = 0;
                    int startIdx = (receivedStatistics_idx + 1) % historyWindow;
                    for (int i = startIdx; i != receivedStatistics_idx; ) {
                        totalData += receivedStatistics[i].elements;
                        queueSize += receivedStatistics[i].queueSize;
                        i = (i + 1) % historyWindow;
                    }
                    runningStats.bitsPerSecond = ((totalData * bitSize) / totalTime);
                    runningStats.elementsPerSecond = (totalData / totalTime);
                    runningStats.averageQueueDepth = (queueSize / historyWindow);
                    runningStats.callsPerSecond = (double(historyWindow - 1) / totalTime);
                    runningStats.timeSinceLastCall = (((double)tv.tv_sec) - front_sec) + (((double)tv.tv_usec - front_usec) / ((double)1e6));
                    unsigned int streamIDsize = activeStreamIDs.size();
                    std::list< std::string >::iterator p = activeStreamIDs.begin();
                    runningStats.streamIDs.length(streamIDsize);
                    for (unsigned int i = 0; i < streamIDsize; i++) {
                        if (p == activeStreamIDs.end()) {
                            break;
                        }
                        runningStats.streamIDs[i] = CORBA::string_dup((*p).c_str());
                        p++;
                    }
                    if ((flush_sec != 0) && (flush_usec != 0)) {
                        double flushTotalTime = (((double)tv.tv_sec) - flush_sec) + (((double)tv.tv_usec - flush_usec) / ((double)1e6));
                        runningStats.keywords.length(1);
                        runningStats.keywords[0].id = CORBA::string_dup("timeSinceLastFlush");
                        runningStats.keywords[0].value <<= CORBA::Double(flushTotalTime);
                    }
                    return runningStats;
                }

            protected:
                bool enabled;
                double bitSize;
                BULKIO::PortStatistics runningStats;
                std::vector<statPoint> receivedStatistics;
                std::list< std::string > activeStreamIDs;
                unsigned long historyWindow;
                long receivedStatistics_idx;
                double flush_sec;
                double flush_usec;
        };
        
        void enableStats(bool enable) {
            stats.setEnabled(enable);
        };


        class dataTransfer
        {
            public:
                dataTransfer(const PortTypes::ShortSequence& data, const BULKIO::PrecisionUTCTime &_T, bool _EOS, const char* _streamID, BULKIO::StreamSRI &_H, bool _sriChanged, bool _inputQueueFlushed)
                {
                    int dataLength = data.length();

#ifdef EXPECTED_VECTOR_IMPL
                    std::_Vector_base<CORBA::Short, _seqVector::seqVectorAllocator<CORBA::Short> >::_Vector_impl *vectorPointer = (std::_Vector_base<CORBA::Short, _seqVector::seqVectorAllocator<CORBA::Short> >::_Vector_impl *) ((void*) & dataBuffer);
                    vectorPointer->_M_start = const_cast<PortTypes::ShortSequence*>(&data)->get_buffer(1);
                    vectorPointer->_M_finish = vectorPointer->_M_start + dataLength;
                    vectorPointer->_M_end_of_storage = vectorPointer->_M_finish;

#else
                    dataBuffer.resize(dataLength);
                    if (dataLength > 0) {
                        memcpy(&dataBuffer[0], &data[0], dataLength * sizeof(data[0]));
                    }

#endif
                    T = _T;
                    EOS = _EOS;
                    streamID = _streamID;
                    SRI = _H;
                    sriChanged = _sriChanged;
                    inputQueueFlushed = _inputQueueFlushed;
                };

#ifdef EXPECTED_VECTOR_IMPL
                std::vector< CORBA::Short, _seqVector::seqVectorAllocator<CORBA::Short> > dataBuffer;
#else
                std::vector<CORBA::Short> dataBuffer;
#endif
                BULKIO::PrecisionUTCTime T;
                bool EOS;
                std::string streamID;
                BULKIO::StreamSRI SRI;
                bool sriChanged;
                bool inputQueueFlushed;
        };

        dataTransfer *getPacket(float timeout);
        void block();
        void unblock();

    protected:
        DataConverter_i *parent;
        std::deque<dataTransfer *> workQueue;
        std::map<std::string, std::pair<BULKIO::StreamSRI, bool> > currentHs;
        boost::mutex dataBufferLock;
        boost::mutex sriUpdateLock;
        omni_mutex dataAvailableMutex;
        omni_condition* dataAvailable;
        unsigned long secs, nsecs, timeout_secs, timeout_nsecs;
        bool breakBlock;
        bool blocking;
        queueSemaphore* queueSem;

        // statistics
        linkStatistics stats;

};



// ----------------------------------------------------------------------------------------
// BULKIO_dataUlong_In_i declaration
// ----------------------------------------------------------------------------------------
class BULKIO_dataUlong_In_i : public POA_BULKIO::dataUlong, public Port_Provides_base_impl
{
    public:
        BULKIO_dataUlong_In_i(std::string port_name, DataConverter_base *_parent);
        ~BULKIO_dataUlong_In_i();

        void pushSRI(const BULKIO::StreamSRI& H);
        void pushPacket(const PortTypes::UlongSequence& data, const BULKIO::PrecisionUTCTime& T, ::CORBA::Boolean EOS, const char* streamID);

        BULKIO::PortUsageType state();
        BULKIO::PortStatistics* statistics();
        BULKIO::StreamSRISequence* activeSRIs();
        int getCurrentQueueDepth();
        int getMaxQueueDepth();
        void setMaxQueueDepth(int newDepth);

        class linkStatistics
        {
            public:
                struct statPoint {
                    unsigned int elements;
                    float queueSize;
                    double secs;
                    double usecs;
                };

                linkStatistics() {
                    bitSize = sizeof(CORBA::ULong) * 8.0;
                    historyWindow = 10;
                    receivedStatistics_idx = 0;
                    receivedStatistics.resize(historyWindow);
                    activeStreamIDs.resize(0);
                    runningStats.elementsPerSecond = -1.0;
                    runningStats.bitsPerSecond = -1.0;
                    runningStats.callsPerSecond = -1.0;
                    runningStats.averageQueueDepth = -1.0;
                    runningStats.streamIDs.length(0);
                    runningStats.timeSinceLastCall = -1;
                    enabled = true;
                    flush_sec = 0;
                    flush_usec = 0;
                };

                ~linkStatistics() {
                }

                void setEnabled(bool enableStats) {
                    enabled = enableStats;
                }

                void update(unsigned int elementsReceived, float queueSize, bool EOS, std::string streamID, bool flush) {
                    if (!enabled) {
                        return;
                    }
                    struct timeval tv;
                    struct timezone tz;
                    gettimeofday(&tv, &tz);
                    receivedStatistics[receivedStatistics_idx].elements = elementsReceived;
                    receivedStatistics[receivedStatistics_idx].queueSize = queueSize;
                    receivedStatistics[receivedStatistics_idx].secs = tv.tv_sec;
                    receivedStatistics[receivedStatistics_idx++].usecs = tv.tv_usec;
                    receivedStatistics_idx = receivedStatistics_idx % historyWindow;
                    if (flush) {
                        flush_sec = tv.tv_sec;
                        flush_usec = tv.tv_usec;
                    }
                    if (!EOS) {
                        std::list<std::string>::iterator p = activeStreamIDs.begin();
                        bool foundStreamID = false;
                        while (p != activeStreamIDs.end()) {
                            if (*p == streamID) {
                                foundStreamID = true;
                                break;
                            }
                            p++;
                        }
                        if (!foundStreamID) {
                            activeStreamIDs.push_back(streamID);
                        }
                    } else {
                        std::list<std::string>::iterator p = activeStreamIDs.begin();
                        while (p != activeStreamIDs.end()) {
                            if (*p == streamID) {
                                activeStreamIDs.erase(p);
                                break;
                            }
                            p++;
                        }
                    }
                }

                BULKIO::PortStatistics retrieve() {
                    if (!enabled) {
                        return runningStats;
                    }
                    struct timeval tv;
                    struct timezone tz;
                    gettimeofday(&tv, &tz);

                    int idx = (receivedStatistics_idx == 0) ? (historyWindow - 1) : (receivedStatistics_idx - 1);
                    double front_sec = receivedStatistics[idx].secs;
                    double front_usec = receivedStatistics[idx].usecs;
                    double secDiff = tv.tv_sec - receivedStatistics[receivedStatistics_idx].secs;
                    double usecDiff = (tv.tv_usec - receivedStatistics[receivedStatistics_idx].usecs) / ((double)1e6);
                    double totalTime = secDiff + usecDiff;
                    double totalData = 0;
                    float queueSize = 0;
                    int startIdx = (receivedStatistics_idx + 1) % historyWindow;
                    for (int i = startIdx; i != receivedStatistics_idx; ) {
                        totalData += receivedStatistics[i].elements;
                        queueSize += receivedStatistics[i].queueSize;
                        i = (i + 1) % historyWindow;
                    }
                    runningStats.bitsPerSecond = ((totalData * bitSize) / totalTime);
                    runningStats.elementsPerSecond = (totalData / totalTime);
                    runningStats.averageQueueDepth = (queueSize / historyWindow);
                    runningStats.callsPerSecond = (double(historyWindow - 1) / totalTime);
                    runningStats.timeSinceLastCall = (((double)tv.tv_sec) - front_sec) + (((double)tv.tv_usec - front_usec) / ((double)1e6));
                    unsigned int streamIDsize = activeStreamIDs.size();
                    std::list< std::string >::iterator p = activeStreamIDs.begin();
                    runningStats.streamIDs.length(streamIDsize);
                    for (unsigned int i = 0; i < streamIDsize; i++) {
                        if (p == activeStreamIDs.end()) {
                            break;
                        }
                        runningStats.streamIDs[i] = CORBA::string_dup((*p).c_str());
                        p++;
                    }
                    if ((flush_sec != 0) && (flush_usec != 0)) {
                        double flushTotalTime = (((double)tv.tv_sec) - flush_sec) + (((double)tv.tv_usec - flush_usec) / ((double)1e6));
                        runningStats.keywords.length(1);
                        runningStats.keywords[0].id = CORBA::string_dup("timeSinceLastFlush");
                        runningStats.keywords[0].value <<= CORBA::Double(flushTotalTime);
                    }
                    return runningStats;
                }

            protected:
                bool enabled;
                double bitSize;
                BULKIO::PortStatistics runningStats;
                std::vector<statPoint> receivedStatistics;
                std::list< std::string > activeStreamIDs;
                unsigned long historyWindow;
                long receivedStatistics_idx;
                double flush_sec;
                double flush_usec;
        };
        
        void enableStats(bool enable) {
            stats.setEnabled(enable);
        };


        class dataTransfer
        {
            public:
                dataTransfer(const PortTypes::UlongSequence& data, const BULKIO::PrecisionUTCTime &_T, bool _EOS, const char* _streamID, BULKIO::StreamSRI &_H, bool _sriChanged, bool _inputQueueFlushed)
                {
                    int dataLength = data.length();

#ifdef EXPECTED_VECTOR_IMPL
                    std::_Vector_base<CORBA::ULong, _seqVector::seqVectorAllocator<CORBA::ULong> >::_Vector_impl *vectorPointer = (std::_Vector_base<CORBA::ULong, _seqVector::seqVectorAllocator<CORBA::ULong> >::_Vector_impl *) ((void*) & dataBuffer);
                    vectorPointer->_M_start = const_cast<PortTypes::UlongSequence*>(&data)->get_buffer(1);
                    vectorPointer->_M_finish = vectorPointer->_M_start + dataLength;
                    vectorPointer->_M_end_of_storage = vectorPointer->_M_finish;

#else
                    dataBuffer.resize(dataLength);
                    if (dataLength > 0) {
                        memcpy(&dataBuffer[0], &data[0], dataLength * sizeof(data[0]));
                    }

#endif
                    T = _T;
                    EOS = _EOS;
                    streamID = _streamID;
                    SRI = _H;
                    sriChanged = _sriChanged;
                    inputQueueFlushed = _inputQueueFlushed;
                };

#ifdef EXPECTED_VECTOR_IMPL
                std::vector< CORBA::ULong, _seqVector::seqVectorAllocator<CORBA::ULong> > dataBuffer;
#else
                std::vector<CORBA::ULong> dataBuffer;
#endif
                BULKIO::PrecisionUTCTime T;
                bool EOS;
                std::string streamID;
                BULKIO::StreamSRI SRI;
                bool sriChanged;
                bool inputQueueFlushed;
        };

        dataTransfer *getPacket(float timeout);
        void block();
        void unblock();

    protected:
        DataConverter_i *parent;
        std::deque<dataTransfer *> workQueue;
        std::map<std::string, std::pair<BULKIO::StreamSRI, bool> > currentHs;
        boost::mutex dataBufferLock;
        boost::mutex sriUpdateLock;
        omni_mutex dataAvailableMutex;
        omni_condition* dataAvailable;
        unsigned long secs, nsecs, timeout_secs, timeout_nsecs;
        bool breakBlock;
        bool blocking;
        queueSemaphore* queueSem;

        // statistics
        linkStatistics stats;

};



// ----------------------------------------------------------------------------------------
// BULKIO_dataDouble_In_i declaration
// ----------------------------------------------------------------------------------------
class BULKIO_dataDouble_In_i : public POA_BULKIO::dataDouble, public Port_Provides_base_impl
{
    public:
        BULKIO_dataDouble_In_i(std::string port_name, DataConverter_base *_parent);
        ~BULKIO_dataDouble_In_i();

        void pushSRI(const BULKIO::StreamSRI& H);
        void pushPacket(const PortTypes::DoubleSequence& data, const BULKIO::PrecisionUTCTime& T, ::CORBA::Boolean EOS, const char* streamID);

        BULKIO::PortUsageType state();
        BULKIO::PortStatistics* statistics();
        BULKIO::StreamSRISequence* activeSRIs();
        int getCurrentQueueDepth();
        int getMaxQueueDepth();
        void setMaxQueueDepth(int newDepth);

        class linkStatistics
        {
            public:
                struct statPoint {
                    unsigned int elements;
                    float queueSize;
                    double secs;
                    double usecs;
                };

                linkStatistics() {
                    bitSize = sizeof(CORBA::Double) * 8.0;
                    historyWindow = 10;
                    receivedStatistics_idx = 0;
                    receivedStatistics.resize(historyWindow);
                    activeStreamIDs.resize(0);
                    runningStats.elementsPerSecond = -1.0;
                    runningStats.bitsPerSecond = -1.0;
                    runningStats.callsPerSecond = -1.0;
                    runningStats.averageQueueDepth = -1.0;
                    runningStats.streamIDs.length(0);
                    runningStats.timeSinceLastCall = -1;
                    enabled = true;
                    flush_sec = 0;
                    flush_usec = 0;
                };

                ~linkStatistics() {
                }

                void setEnabled(bool enableStats) {
                    enabled = enableStats;
                }

                void update(unsigned int elementsReceived, float queueSize, bool EOS, std::string streamID, bool flush) {
                    if (!enabled) {
                        return;
                    }
                    struct timeval tv;
                    struct timezone tz;
                    gettimeofday(&tv, &tz);
                    receivedStatistics[receivedStatistics_idx].elements = elementsReceived;
                    receivedStatistics[receivedStatistics_idx].queueSize = queueSize;
                    receivedStatistics[receivedStatistics_idx].secs = tv.tv_sec;
                    receivedStatistics[receivedStatistics_idx++].usecs = tv.tv_usec;
                    receivedStatistics_idx = receivedStatistics_idx % historyWindow;
                    if (flush) {
                        flush_sec = tv.tv_sec;
                        flush_usec = tv.tv_usec;
                    }
                    if (!EOS) {
                        std::list<std::string>::iterator p = activeStreamIDs.begin();
                        bool foundStreamID = false;
                        while (p != activeStreamIDs.end()) {
                            if (*p == streamID) {
                                foundStreamID = true;
                                break;
                            }
                            p++;
                        }
                        if (!foundStreamID) {
                            activeStreamIDs.push_back(streamID);
                        }
                    } else {
                        std::list<std::string>::iterator p = activeStreamIDs.begin();
                        while (p != activeStreamIDs.end()) {
                            if (*p == streamID) {
                                activeStreamIDs.erase(p);
                                break;
                            }
                            p++;
                        }
                    }
                }

                BULKIO::PortStatistics retrieve() {
                    if (!enabled) {
                        return runningStats;
                    }
                    struct timeval tv;
                    struct timezone tz;
                    gettimeofday(&tv, &tz);

                    int idx = (receivedStatistics_idx == 0) ? (historyWindow - 1) : (receivedStatistics_idx - 1);
                    double front_sec = receivedStatistics[idx].secs;
                    double front_usec = receivedStatistics[idx].usecs;
                    double secDiff = tv.tv_sec - receivedStatistics[receivedStatistics_idx].secs;
                    double usecDiff = (tv.tv_usec - receivedStatistics[receivedStatistics_idx].usecs) / ((double)1e6);
                    double totalTime = secDiff + usecDiff;
                    double totalData = 0;
                    float queueSize = 0;
                    int startIdx = (receivedStatistics_idx + 1) % historyWindow;
                    for (int i = startIdx; i != receivedStatistics_idx; ) {
                        totalData += receivedStatistics[i].elements;
                        queueSize += receivedStatistics[i].queueSize;
                        i = (i + 1) % historyWindow;
                    }
                    runningStats.bitsPerSecond = ((totalData * bitSize) / totalTime);
                    runningStats.elementsPerSecond = (totalData / totalTime);
                    runningStats.averageQueueDepth = (queueSize / historyWindow);
                    runningStats.callsPerSecond = (double(historyWindow - 1) / totalTime);
                    runningStats.timeSinceLastCall = (((double)tv.tv_sec) - front_sec) + (((double)tv.tv_usec - front_usec) / ((double)1e6));
                    unsigned int streamIDsize = activeStreamIDs.size();
                    std::list< std::string >::iterator p = activeStreamIDs.begin();
                    runningStats.streamIDs.length(streamIDsize);
                    for (unsigned int i = 0; i < streamIDsize; i++) {
                        if (p == activeStreamIDs.end()) {
                            break;
                        }
                        runningStats.streamIDs[i] = CORBA::string_dup((*p).c_str());
                        p++;
                    }
                    if ((flush_sec != 0) && (flush_usec != 0)) {
                        double flushTotalTime = (((double)tv.tv_sec) - flush_sec) + (((double)tv.tv_usec - flush_usec) / ((double)1e6));
                        runningStats.keywords.length(1);
                        runningStats.keywords[0].id = CORBA::string_dup("timeSinceLastFlush");
                        runningStats.keywords[0].value <<= CORBA::Double(flushTotalTime);
                    }
                    return runningStats;
                }

            protected:
                bool enabled;
                double bitSize;
                BULKIO::PortStatistics runningStats;
                std::vector<statPoint> receivedStatistics;
                std::list< std::string > activeStreamIDs;
                unsigned long historyWindow;
                long receivedStatistics_idx;
                double flush_sec;
                double flush_usec;
        };
        
        void enableStats(bool enable) {
            stats.setEnabled(enable);
        };


        class dataTransfer
        {
            public:
                dataTransfer(const PortTypes::DoubleSequence& data, const BULKIO::PrecisionUTCTime &_T, bool _EOS, const char* _streamID, BULKIO::StreamSRI &_H, bool _sriChanged, bool _inputQueueFlushed)
                {
                    int dataLength = data.length();

#ifdef EXPECTED_VECTOR_IMPL
                    std::_Vector_base<CORBA::Double, _seqVector::seqVectorAllocator<CORBA::Double> >::_Vector_impl *vectorPointer = (std::_Vector_base<CORBA::Double, _seqVector::seqVectorAllocator<CORBA::Double> >::_Vector_impl *) ((void*) & dataBuffer);
                    vectorPointer->_M_start = const_cast<PortTypes::DoubleSequence*>(&data)->get_buffer(1);
                    vectorPointer->_M_finish = vectorPointer->_M_start + dataLength;
                    vectorPointer->_M_end_of_storage = vectorPointer->_M_finish;

#else
                    dataBuffer.resize(dataLength);
                    if (dataLength > 0) {
                        memcpy(&dataBuffer[0], &data[0], dataLength * sizeof(data[0]));
                    }

#endif
                    T = _T;
                    EOS = _EOS;
                    streamID = _streamID;
                    SRI = _H;
                    sriChanged = _sriChanged;
                    inputQueueFlushed = _inputQueueFlushed;
                };

#ifdef EXPECTED_VECTOR_IMPL
                std::vector< CORBA::Double, _seqVector::seqVectorAllocator<CORBA::Double> > dataBuffer;
#else
                std::vector<CORBA::Double> dataBuffer;
#endif
                BULKIO::PrecisionUTCTime T;
                bool EOS;
                std::string streamID;
                BULKIO::StreamSRI SRI;
                bool sriChanged;
                bool inputQueueFlushed;
        };

        dataTransfer *getPacket(float timeout);
        void block();
        void unblock();

    protected:
        DataConverter_i *parent;
        std::deque<dataTransfer *> workQueue;
        std::map<std::string, std::pair<BULKIO::StreamSRI, bool> > currentHs;
        boost::mutex dataBufferLock;
        boost::mutex sriUpdateLock;
        omni_mutex dataAvailableMutex;
        omni_condition* dataAvailable;
        unsigned long secs, nsecs, timeout_secs, timeout_nsecs;
        bool breakBlock;
        bool blocking;
        queueSemaphore* queueSem;

        // statistics
        linkStatistics stats;

};



// ----------------------------------------------------------------------------------------
// BULKIO_dataFloat_In_i declaration
// ----------------------------------------------------------------------------------------
class BULKIO_dataFloat_In_i : public POA_BULKIO::dataFloat, public Port_Provides_base_impl
{
    public:
        BULKIO_dataFloat_In_i(std::string port_name, DataConverter_base *_parent);
        ~BULKIO_dataFloat_In_i();

        void pushSRI(const BULKIO::StreamSRI& H);
        void pushPacket(const PortTypes::FloatSequence& data, const BULKIO::PrecisionUTCTime& T, ::CORBA::Boolean EOS, const char* streamID);

        BULKIO::PortUsageType state();
        BULKIO::PortStatistics* statistics();
        BULKIO::StreamSRISequence* activeSRIs();
        int getCurrentQueueDepth();
        int getMaxQueueDepth();
        void setMaxQueueDepth(int newDepth);

        class linkStatistics
        {
            public:
                struct statPoint {
                    unsigned int elements;
                    float queueSize;
                    double secs;
                    double usecs;
                };

                linkStatistics() {
                    bitSize = sizeof(CORBA::Float) * 8.0;
                    historyWindow = 10;
                    receivedStatistics_idx = 0;
                    receivedStatistics.resize(historyWindow);
                    activeStreamIDs.resize(0);
                    runningStats.elementsPerSecond = -1.0;
                    runningStats.bitsPerSecond = -1.0;
                    runningStats.callsPerSecond = -1.0;
                    runningStats.averageQueueDepth = -1.0;
                    runningStats.streamIDs.length(0);
                    runningStats.timeSinceLastCall = -1;
                    enabled = true;
                    flush_sec = 0;
                    flush_usec = 0;
                };

                ~linkStatistics() {
                }

                void setEnabled(bool enableStats) {
                    enabled = enableStats;
                }

                void update(unsigned int elementsReceived, float queueSize, bool EOS, std::string streamID, bool flush) {
                    if (!enabled) {
                        return;
                    }
                    struct timeval tv;
                    struct timezone tz;
                    gettimeofday(&tv, &tz);
                    receivedStatistics[receivedStatistics_idx].elements = elementsReceived;
                    receivedStatistics[receivedStatistics_idx].queueSize = queueSize;
                    receivedStatistics[receivedStatistics_idx].secs = tv.tv_sec;
                    receivedStatistics[receivedStatistics_idx++].usecs = tv.tv_usec;
                    receivedStatistics_idx = receivedStatistics_idx % historyWindow;
                    if (flush) {
                        flush_sec = tv.tv_sec;
                        flush_usec = tv.tv_usec;
                    }
                    if (!EOS) {
                        std::list<std::string>::iterator p = activeStreamIDs.begin();
                        bool foundStreamID = false;
                        while (p != activeStreamIDs.end()) {
                            if (*p == streamID) {
                                foundStreamID = true;
                                break;
                            }
                            p++;
                        }
                        if (!foundStreamID) {
                            activeStreamIDs.push_back(streamID);
                        }
                    } else {
                        std::list<std::string>::iterator p = activeStreamIDs.begin();
                        while (p != activeStreamIDs.end()) {
                            if (*p == streamID) {
                                activeStreamIDs.erase(p);
                                break;
                            }
                            p++;
                        }
                    }
                }

                BULKIO::PortStatistics retrieve() {
                    if (!enabled) {
                        return runningStats;
                    }
                    struct timeval tv;
                    struct timezone tz;
                    gettimeofday(&tv, &tz);

                    int idx = (receivedStatistics_idx == 0) ? (historyWindow - 1) : (receivedStatistics_idx - 1);
                    double front_sec = receivedStatistics[idx].secs;
                    double front_usec = receivedStatistics[idx].usecs;
                    double secDiff = tv.tv_sec - receivedStatistics[receivedStatistics_idx].secs;
                    double usecDiff = (tv.tv_usec - receivedStatistics[receivedStatistics_idx].usecs) / ((double)1e6);
                    double totalTime = secDiff + usecDiff;
                    double totalData = 0;
                    float queueSize = 0;
                    int startIdx = (receivedStatistics_idx + 1) % historyWindow;
                    for (int i = startIdx; i != receivedStatistics_idx; ) {
                        totalData += receivedStatistics[i].elements;
                        queueSize += receivedStatistics[i].queueSize;
                        i = (i + 1) % historyWindow;
                    }
                    runningStats.bitsPerSecond = ((totalData * bitSize) / totalTime);
                    runningStats.elementsPerSecond = (totalData / totalTime);
                    runningStats.averageQueueDepth = (queueSize / historyWindow);
                    runningStats.callsPerSecond = (double(historyWindow - 1) / totalTime);
                    runningStats.timeSinceLastCall = (((double)tv.tv_sec) - front_sec) + (((double)tv.tv_usec - front_usec) / ((double)1e6));
                    unsigned int streamIDsize = activeStreamIDs.size();
                    std::list< std::string >::iterator p = activeStreamIDs.begin();
                    runningStats.streamIDs.length(streamIDsize);
                    for (unsigned int i = 0; i < streamIDsize; i++) {
                        if (p == activeStreamIDs.end()) {
                            break;
                        }
                        runningStats.streamIDs[i] = CORBA::string_dup((*p).c_str());
                        p++;
                    }
                    if ((flush_sec != 0) && (flush_usec != 0)) {
                        double flushTotalTime = (((double)tv.tv_sec) - flush_sec) + (((double)tv.tv_usec - flush_usec) / ((double)1e6));
                        runningStats.keywords.length(1);
                        runningStats.keywords[0].id = CORBA::string_dup("timeSinceLastFlush");
                        runningStats.keywords[0].value <<= CORBA::Double(flushTotalTime);
                    }
                    return runningStats;
                }

            protected:
                bool enabled;
                double bitSize;
                BULKIO::PortStatistics runningStats;
                std::vector<statPoint> receivedStatistics;
                std::list< std::string > activeStreamIDs;
                unsigned long historyWindow;
                long receivedStatistics_idx;
                double flush_sec;
                double flush_usec;
        };
        
        void enableStats(bool enable) {
            stats.setEnabled(enable);
        };


        class dataTransfer
        {
            public:
                dataTransfer(const PortTypes::FloatSequence& data, const BULKIO::PrecisionUTCTime &_T, bool _EOS, const char* _streamID, BULKIO::StreamSRI &_H, bool _sriChanged, bool _inputQueueFlushed)
                {
                    int dataLength = data.length();

#ifdef EXPECTED_VECTOR_IMPL
                    std::_Vector_base<CORBA::Float, _seqVector::seqVectorAllocator<CORBA::Float> >::_Vector_impl *vectorPointer = (std::_Vector_base<CORBA::Float, _seqVector::seqVectorAllocator<CORBA::Float> >::_Vector_impl *) ((void*) & dataBuffer);
                    vectorPointer->_M_start = const_cast<PortTypes::FloatSequence*>(&data)->get_buffer(1);
                    vectorPointer->_M_finish = vectorPointer->_M_start + dataLength;
                    vectorPointer->_M_end_of_storage = vectorPointer->_M_finish;

#else
                    dataBuffer.resize(dataLength);
                    if (dataLength > 0) {
                        memcpy(&dataBuffer[0], &data[0], dataLength * sizeof(data[0]));
                    }

#endif
                    T = _T;
                    EOS = _EOS;
                    streamID = _streamID;
                    SRI = _H;
                    sriChanged = _sriChanged;
                    inputQueueFlushed = _inputQueueFlushed;
                };

#ifdef EXPECTED_VECTOR_IMPL
                std::vector< CORBA::Float, _seqVector::seqVectorAllocator<CORBA::Float> > dataBuffer;
#else
                std::vector<CORBA::Float> dataBuffer;
#endif
                BULKIO::PrecisionUTCTime T;
                bool EOS;
                std::string streamID;
                BULKIO::StreamSRI SRI;
                bool sriChanged;
                bool inputQueueFlushed;
        };

        dataTransfer *getPacket(float timeout);
        void block();
        void unblock();

    protected:
        DataConverter_i *parent;
        std::deque<dataTransfer *> workQueue;
        std::map<std::string, std::pair<BULKIO::StreamSRI, bool> > currentHs;
        boost::mutex dataBufferLock;
        boost::mutex sriUpdateLock;
        omni_mutex dataAvailableMutex;
        omni_condition* dataAvailable;
        unsigned long secs, nsecs, timeout_secs, timeout_nsecs;
        bool breakBlock;
        bool blocking;
        queueSemaphore* queueSem;

        // statistics
        linkStatistics stats;

};



// ----------------------------------------------------------------------------------------
// BULKIO_dataLong_In_i declaration
// ----------------------------------------------------------------------------------------
class BULKIO_dataLong_In_i : public POA_BULKIO::dataLong, public Port_Provides_base_impl
{
    public:
        BULKIO_dataLong_In_i(std::string port_name, DataConverter_base *_parent);
        ~BULKIO_dataLong_In_i();

        void pushSRI(const BULKIO::StreamSRI& H);
        void pushPacket(const PortTypes::LongSequence& data, const BULKIO::PrecisionUTCTime& T, ::CORBA::Boolean EOS, const char* streamID);

        BULKIO::PortUsageType state();
        BULKIO::PortStatistics* statistics();
        BULKIO::StreamSRISequence* activeSRIs();
        int getCurrentQueueDepth();
        int getMaxQueueDepth();
        void setMaxQueueDepth(int newDepth);

        class linkStatistics
        {
            public:
                struct statPoint {
                    unsigned int elements;
                    float queueSize;
                    double secs;
                    double usecs;
                };

                linkStatistics() {
                    bitSize = sizeof(CORBA::Long) * 8.0;
                    historyWindow = 10;
                    receivedStatistics_idx = 0;
                    receivedStatistics.resize(historyWindow);
                    activeStreamIDs.resize(0);
                    runningStats.elementsPerSecond = -1.0;
                    runningStats.bitsPerSecond = -1.0;
                    runningStats.callsPerSecond = -1.0;
                    runningStats.averageQueueDepth = -1.0;
                    runningStats.streamIDs.length(0);
                    runningStats.timeSinceLastCall = -1;
                    enabled = true;
                    flush_sec = 0;
                    flush_usec = 0;
                };

                ~linkStatistics() {
                }

                void setEnabled(bool enableStats) {
                    enabled = enableStats;
                }

                void update(unsigned int elementsReceived, float queueSize, bool EOS, std::string streamID, bool flush) {
                    if (!enabled) {
                        return;
                    }
                    struct timeval tv;
                    struct timezone tz;
                    gettimeofday(&tv, &tz);
                    receivedStatistics[receivedStatistics_idx].elements = elementsReceived;
                    receivedStatistics[receivedStatistics_idx].queueSize = queueSize;
                    receivedStatistics[receivedStatistics_idx].secs = tv.tv_sec;
                    receivedStatistics[receivedStatistics_idx++].usecs = tv.tv_usec;
                    receivedStatistics_idx = receivedStatistics_idx % historyWindow;
                    if (flush) {
                        flush_sec = tv.tv_sec;
                        flush_usec = tv.tv_usec;
                    }
                    if (!EOS) {
                        std::list<std::string>::iterator p = activeStreamIDs.begin();
                        bool foundStreamID = false;
                        while (p != activeStreamIDs.end()) {
                            if (*p == streamID) {
                                foundStreamID = true;
                                break;
                            }
                            p++;
                        }
                        if (!foundStreamID) {
                            activeStreamIDs.push_back(streamID);
                        }
                    } else {
                        std::list<std::string>::iterator p = activeStreamIDs.begin();
                        while (p != activeStreamIDs.end()) {
                            if (*p == streamID) {
                                activeStreamIDs.erase(p);
                                break;
                            }
                            p++;
                        }
                    }
                }

                BULKIO::PortStatistics retrieve() {
                    if (!enabled) {
                        return runningStats;
                    }
                    struct timeval tv;
                    struct timezone tz;
                    gettimeofday(&tv, &tz);

                    int idx = (receivedStatistics_idx == 0) ? (historyWindow - 1) : (receivedStatistics_idx - 1);
                    double front_sec = receivedStatistics[idx].secs;
                    double front_usec = receivedStatistics[idx].usecs;
                    double secDiff = tv.tv_sec - receivedStatistics[receivedStatistics_idx].secs;
                    double usecDiff = (tv.tv_usec - receivedStatistics[receivedStatistics_idx].usecs) / ((double)1e6);
                    double totalTime = secDiff + usecDiff;
                    double totalData = 0;
                    float queueSize = 0;
                    int startIdx = (receivedStatistics_idx + 1) % historyWindow;
                    for (int i = startIdx; i != receivedStatistics_idx; ) {
                        totalData += receivedStatistics[i].elements;
                        queueSize += receivedStatistics[i].queueSize;
                        i = (i + 1) % historyWindow;
                    }
                    runningStats.bitsPerSecond = ((totalData * bitSize) / totalTime);
                    runningStats.elementsPerSecond = (totalData / totalTime);
                    runningStats.averageQueueDepth = (queueSize / historyWindow);
                    runningStats.callsPerSecond = (double(historyWindow - 1) / totalTime);
                    runningStats.timeSinceLastCall = (((double)tv.tv_sec) - front_sec) + (((double)tv.tv_usec - front_usec) / ((double)1e6));
                    unsigned int streamIDsize = activeStreamIDs.size();
                    std::list< std::string >::iterator p = activeStreamIDs.begin();
                    runningStats.streamIDs.length(streamIDsize);
                    for (unsigned int i = 0; i < streamIDsize; i++) {
                        if (p == activeStreamIDs.end()) {
                            break;
                        }
                        runningStats.streamIDs[i] = CORBA::string_dup((*p).c_str());
                        p++;
                    }
                    if ((flush_sec != 0) && (flush_usec != 0)) {
                        double flushTotalTime = (((double)tv.tv_sec) - flush_sec) + (((double)tv.tv_usec - flush_usec) / ((double)1e6));
                        runningStats.keywords.length(1);
                        runningStats.keywords[0].id = CORBA::string_dup("timeSinceLastFlush");
                        runningStats.keywords[0].value <<= CORBA::Double(flushTotalTime);
                    }
                    return runningStats;
                }

            protected:
                bool enabled;
                double bitSize;
                BULKIO::PortStatistics runningStats;
                std::vector<statPoint> receivedStatistics;
                std::list< std::string > activeStreamIDs;
                unsigned long historyWindow;
                long receivedStatistics_idx;
                double flush_sec;
                double flush_usec;
        };
        
        void enableStats(bool enable) {
            stats.setEnabled(enable);
        };


        class dataTransfer
        {
            public:
                dataTransfer(const PortTypes::LongSequence& data, const BULKIO::PrecisionUTCTime &_T, bool _EOS, const char* _streamID, BULKIO::StreamSRI &_H, bool _sriChanged, bool _inputQueueFlushed)
                {
                    int dataLength = data.length();

#ifdef EXPECTED_VECTOR_IMPL
                    std::_Vector_base<CORBA::Long, _seqVector::seqVectorAllocator<CORBA::Long> >::_Vector_impl *vectorPointer = (std::_Vector_base<CORBA::Long, _seqVector::seqVectorAllocator<CORBA::Long> >::_Vector_impl *) ((void*) & dataBuffer);
                    vectorPointer->_M_start = const_cast<PortTypes::LongSequence*>(&data)->get_buffer(1);
                    vectorPointer->_M_finish = vectorPointer->_M_start + dataLength;
                    vectorPointer->_M_end_of_storage = vectorPointer->_M_finish;

#else
                    dataBuffer.resize(dataLength);
                    if (dataLength > 0) {
                        memcpy(&dataBuffer[0], &data[0], dataLength * sizeof(data[0]));
                    }

#endif
                    T = _T;
                    EOS = _EOS;
                    streamID = _streamID;
                    SRI = _H;
                    sriChanged = _sriChanged;
                    inputQueueFlushed = _inputQueueFlushed;
                };

#ifdef EXPECTED_VECTOR_IMPL
                std::vector< CORBA::Long, _seqVector::seqVectorAllocator<CORBA::Long> > dataBuffer;
#else
                std::vector<CORBA::Long> dataBuffer;
#endif
                BULKIO::PrecisionUTCTime T;
                bool EOS;
                std::string streamID;
                BULKIO::StreamSRI SRI;
                bool sriChanged;
                bool inputQueueFlushed;
        };

        dataTransfer *getPacket(float timeout);
        void block();
        void unblock();

    protected:
        DataConverter_i *parent;
        std::deque<dataTransfer *> workQueue;
        std::map<std::string, std::pair<BULKIO::StreamSRI, bool> > currentHs;
        boost::mutex dataBufferLock;
        boost::mutex sriUpdateLock;
        omni_mutex dataAvailableMutex;
        omni_condition* dataAvailable;
        unsigned long secs, nsecs, timeout_secs, timeout_nsecs;
        bool breakBlock;
        bool blocking;
        queueSemaphore* queueSem;

        // statistics
        linkStatistics stats;

};



// ----------------------------------------------------------------------------------------
// BULKIO_dataOctet_In_i declaration
// ----------------------------------------------------------------------------------------
class BULKIO_dataOctet_In_i : public POA_BULKIO::dataOctet, public Port_Provides_base_impl
{
    public:
        BULKIO_dataOctet_In_i(std::string port_name, DataConverter_base *_parent);
        ~BULKIO_dataOctet_In_i();

        void pushSRI(const BULKIO::StreamSRI& H);
        void pushPacket(const CF::OctetSequence& data, const BULKIO::PrecisionUTCTime& T, ::CORBA::Boolean EOS, const char* streamID);

        BULKIO::PortUsageType state();
        BULKIO::PortStatistics* statistics();
        BULKIO::StreamSRISequence* activeSRIs();
        int getCurrentQueueDepth();
        int getMaxQueueDepth();
        void setMaxQueueDepth(int newDepth);

        class linkStatistics
        {
            public:
                struct statPoint {
                    unsigned int elements;
                    float queueSize;
                    double secs;
                    double usecs;
                };

                linkStatistics() {
                    bitSize = sizeof(unsigned char) * 8.0;
                    historyWindow = 10;
                    receivedStatistics_idx = 0;
                    receivedStatistics.resize(historyWindow);
                    activeStreamIDs.resize(0);
                    runningStats.elementsPerSecond = -1.0;
                    runningStats.bitsPerSecond = -1.0;
                    runningStats.callsPerSecond = -1.0;
                    runningStats.averageQueueDepth = -1.0;
                    runningStats.streamIDs.length(0);
                    runningStats.timeSinceLastCall = -1;
                    enabled = true;
                    flush_sec = 0;
                    flush_usec = 0;
                };

                ~linkStatistics() {
                }

                void setEnabled(bool enableStats) {
                    enabled = enableStats;
                }

                void update(unsigned int elementsReceived, float queueSize, bool EOS, std::string streamID, bool flush) {
                    if (!enabled) {
                        return;
                    }
                    struct timeval tv;
                    struct timezone tz;
                    gettimeofday(&tv, &tz);
                    receivedStatistics[receivedStatistics_idx].elements = elementsReceived;
                    receivedStatistics[receivedStatistics_idx].queueSize = queueSize;
                    receivedStatistics[receivedStatistics_idx].secs = tv.tv_sec;
                    receivedStatistics[receivedStatistics_idx++].usecs = tv.tv_usec;
                    receivedStatistics_idx = receivedStatistics_idx % historyWindow;
                    if (flush) {
                        flush_sec = tv.tv_sec;
                        flush_usec = tv.tv_usec;
                    }
                    if (!EOS) {
                        std::list<std::string>::iterator p = activeStreamIDs.begin();
                        bool foundStreamID = false;
                        while (p != activeStreamIDs.end()) {
                            if (*p == streamID) {
                                foundStreamID = true;
                                break;
                            }
                            p++;
                        }
                        if (!foundStreamID) {
                            activeStreamIDs.push_back(streamID);
                        }
                    } else {
                        std::list<std::string>::iterator p = activeStreamIDs.begin();
                        while (p != activeStreamIDs.end()) {
                            if (*p == streamID) {
                                activeStreamIDs.erase(p);
                                break;
                            }
                            p++;
                        }
                    }
                }

                BULKIO::PortStatistics retrieve() {
                    if (!enabled) {
                        return runningStats;
                    }
                    struct timeval tv;
                    struct timezone tz;
                    gettimeofday(&tv, &tz);

                    int idx = (receivedStatistics_idx == 0) ? (historyWindow - 1) : (receivedStatistics_idx - 1);
                    double front_sec = receivedStatistics[idx].secs;
                    double front_usec = receivedStatistics[idx].usecs;
                    double secDiff = tv.tv_sec - receivedStatistics[receivedStatistics_idx].secs;
                    double usecDiff = (tv.tv_usec - receivedStatistics[receivedStatistics_idx].usecs) / ((double)1e6);
                    double totalTime = secDiff + usecDiff;
                    double totalData = 0;
                    float queueSize = 0;
                    int startIdx = (receivedStatistics_idx + 1) % historyWindow;
                    for (int i = startIdx; i != receivedStatistics_idx; ) {
                        totalData += receivedStatistics[i].elements;
                        queueSize += receivedStatistics[i].queueSize;
                        i = (i + 1) % historyWindow;
                    }
                    runningStats.bitsPerSecond = ((totalData * bitSize) / totalTime);
                    runningStats.elementsPerSecond = (totalData / totalTime);
                    runningStats.averageQueueDepth = (queueSize / historyWindow);
                    runningStats.callsPerSecond = (double(historyWindow - 1) / totalTime);
                    runningStats.timeSinceLastCall = (((double)tv.tv_sec) - front_sec) + (((double)tv.tv_usec - front_usec) / ((double)1e6));
                    unsigned int streamIDsize = activeStreamIDs.size();
                    std::list< std::string >::iterator p = activeStreamIDs.begin();
                    runningStats.streamIDs.length(streamIDsize);
                    for (unsigned int i = 0; i < streamIDsize; i++) {
                        if (p == activeStreamIDs.end()) {
                            break;
                        }
                        runningStats.streamIDs[i] = CORBA::string_dup((*p).c_str());
                        p++;
                    }
                    if ((flush_sec != 0) && (flush_usec != 0)) {
                        double flushTotalTime = (((double)tv.tv_sec) - flush_sec) + (((double)tv.tv_usec - flush_usec) / ((double)1e6));
                        runningStats.keywords.length(1);
                        runningStats.keywords[0].id = CORBA::string_dup("timeSinceLastFlush");
                        runningStats.keywords[0].value <<= CORBA::Double(flushTotalTime);
                    }
                    return runningStats;
                }

            protected:
                bool enabled;
                double bitSize;
                BULKIO::PortStatistics runningStats;
                std::vector<statPoint> receivedStatistics;
                std::list< std::string > activeStreamIDs;
                unsigned long historyWindow;
                long receivedStatistics_idx;
                double flush_sec;
                double flush_usec;
        };
        
        void enableStats(bool enable) {
            stats.setEnabled(enable);
        };


        class dataTransfer
        {
            public:
                dataTransfer(const CF::OctetSequence& data, const BULKIO::PrecisionUTCTime &_T, bool _EOS, const char* _streamID, BULKIO::StreamSRI &_H, bool _sriChanged, bool _inputQueueFlushed)
                {
                    int dataLength = data.length();

#ifdef EXPECTED_VECTOR_IMPL
                    std::_Vector_base<unsigned char, _seqVector::seqVectorAllocator<unsigned char> >::_Vector_impl *vectorPointer = (std::_Vector_base<unsigned char, _seqVector::seqVectorAllocator<unsigned char> >::_Vector_impl *) ((void*) & dataBuffer);
                    vectorPointer->_M_start = const_cast<CF::OctetSequence*>(&data)->get_buffer(1);
                    vectorPointer->_M_finish = vectorPointer->_M_start + dataLength;
                    vectorPointer->_M_end_of_storage = vectorPointer->_M_finish;

#else
                    dataBuffer.resize(dataLength);
                    if (dataLength > 0) {
                        memcpy(&dataBuffer[0], &data[0], dataLength * sizeof(data[0]));
                    }

#endif
                    T = _T;
                    EOS = _EOS;
                    streamID = _streamID;
                    SRI = _H;
                    sriChanged = _sriChanged;
                    inputQueueFlushed = _inputQueueFlushed;
                };

#ifdef EXPECTED_VECTOR_IMPL
                std::vector< unsigned char, _seqVector::seqVectorAllocator<unsigned char> > dataBuffer;
#else
                std::vector<unsigned char> dataBuffer;
#endif
                BULKIO::PrecisionUTCTime T;
                bool EOS;
                std::string streamID;
                BULKIO::StreamSRI SRI;
                bool sriChanged;
                bool inputQueueFlushed;
        };

        dataTransfer *getPacket(float timeout);
        void block();
        void unblock();

    protected:
        DataConverter_i *parent;
        std::deque<dataTransfer *> workQueue;
        std::map<std::string, std::pair<BULKIO::StreamSRI, bool> > currentHs;
        boost::mutex dataBufferLock;
        boost::mutex sriUpdateLock;
        omni_mutex dataAvailableMutex;
        omni_condition* dataAvailable;
        unsigned long secs, nsecs, timeout_secs, timeout_nsecs;
        bool breakBlock;
        bool blocking;
        queueSemaphore* queueSem;

        // statistics
        linkStatistics stats;

};

#endif
