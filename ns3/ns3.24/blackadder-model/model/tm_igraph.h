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

#ifndef TM_IGRAPH_HH
#define TM_IGRAPH_HH

#include <map>
#include <set>
#include <string>

#include <igraph/igraph.h>
#include <climits>
#include <stdlib.h>
#include <iostream>
#include <fstream>

#include "bit-vector.h"
#include "service-model.h"
#include "igraph_version.h"



using namespace ns3;
using namespace std;

/**@brief (Topology Manager) This is a representation of the network topology (using the iGraph library) for the Topology Manager.
 */
class TMIgraph {
public:
    /**@brief Contructor: creates an empty iGraph graph.
     */
    TMIgraph();
    /**@brief destroys the graph and frees some meomry here and there.
     * 
     */
    ~TMIgraph();
    /**@brief reads the graphml file that describes the network topology and creates some indexes for making the operation faster.
     * 
     * Currently iGraph cannot read the Global Graph variables and, therefore, this methid does it manually.
     * 
     * @param name the /graphML file name
     * @return <0 if there was a problem reading the file
     */
    int readTopology(const char *name);
    /**@brief it calculates a LIPSIN identifier from source to destination using the shortest path.
     * 
     * @param source the node label of the source node.
     * @param destination the node label of the destination node.
     * @return a pointer to a BitVector that represents the LIPSIN identifier.
     */
    Ptr<BitVector>calculateFID(string &source, string &destination);
    /**@brief it calculates LIPSIN identifiers from a set of publishers to a set of subscribers using the shortest paths.
     * 
     * @param publishers a reference to a set of node labels, representing the source nodes.
     * @param subscribers a reference to a set of node labels, representing the destination nodes.
     * @param result a reference to a map where the method will put node labels representing source nodes mapped to LIPSIN identifiers. Note that some of these identifiers may be NULL.
     */
    void calculateFID(set<string> &publishers, set<string> &subscribers, map<string, Ptr<BitVector > > &result);
    /**@brief used internally by the above method.
     * 
     * @param source
     * @param destination
     * @param resultFID
     * @param numberOfHops
     */
    void calculateFID(string &source, string &destination, BitVector &resultFID, unsigned int &numberOfHops);
    /**@brief the igraph graph
     */
    igraph_t graph;
    /**@brief the node label of this Blackadder node.
     */
    string nodeID;
    /**@brief number of connections in the graph.
     */
    int number_of_connections;
    /**@brief number of nodes in the graph.
     */
    int number_of_nodes;
    /**@brief an index that maps node labels to igraph vertex ids.
     */
    map<string, int> reverse_node_index;
    /**@brief an index that maps node labels to igraph edge ids.
     */
    map<string, int> reverse_edge_index;
    /**@brief an index that maps node labels to their internal link Identifiers.
     */
    map<string, Ptr<BitVector> > nodeID_iLID;
    /**@brief an index that maps igraph vertex ids to internal link Identifiers.
     */
    map<int, Ptr<BitVector> > vertex_iLID;
    /**@brief an index that maps igraph edge ids to link Identifiers.
     */
    map<int, Ptr<BitVector> > edge_LID;
    /**@brief the length in bytes of the LIPSIN identifier.
     */
    int fid_len;
    /**@brief mode in which this Blackadder node runs - the TM must create the appropriate netlink socket.
     */
    string mode;
};

#endif
