#ifndef PATHFINDER_HPP_INCLUDED
#define PATHFINDER_HPP_INCLUDED

#include "stdafx.h"

#include "fwd/pathfinder.hpp"
#include "map.hpp"
#include "npc.hpp"
#include "util.hpp"
#include "world.hpp"

const int MAX_WIDTH = 255;
const int MAX_HEIGHT = 255;
const int DIRECTIONS = 4;
static int squares[MAX_WIDTH][MAX_HEIGHT] = {0};
static int closedNodes[MAX_WIDTH][MAX_HEIGHT] = {0};
static int openNodes[MAX_WIDTH][MAX_HEIGHT] = {0};
static int dirMap[MAX_WIDTH][MAX_HEIGHT] = {0};
// if NDIR = 4
const int xdir[DIRECTIONS] = {1, 0, -1, 0};//IDIR
const int ydir[DIRECTIONS] = {0, 1, 0, -1};//JDIR
// if NDIR = 8
//const int iDir[NDIR] = {1, 1, 0, -1, -1, -1, 0, 1};
//const int jDir[NDIR] = {0, 1, 1, 1, 0, -1, -1, -1};


using namespace std;

struct Location
{
    int row, col;

    Location()
    {
        row = col = 0;
    };

    Location(int r, int c)
    {
        row = r;
        col = c;
    };
};

// current position
class Node
{
    public:
    Node(const Location &loc, int g, int f);
    int rPos;
    int cPos;
    // total distance already travelled to reach the node
    int GValue;
    // FValue = GValue + remaining distance estimate
    int FValue;  // smaller FValue gets priority
    Location getLocation() const {return Location(rPos,cPos);}
    int getGValue() const {return GValue;}
    int getFValue() const {return FValue;}

    void calculateFValue(const Location& locDest,int type)
    {
        FValue = GValue + getHValue(locDest,type) * 10;
    }

    void updateGValue(const int & i) // i: direction
    {
        GValue += (DIRECTIONS == 8 ? (i % 2 == 0 ? 10 : 14) : 10);
        //GValue += 10;
    }

    // Estimation function for the remaining distance to the goal.
    const int & getHValue(const Location& locDest,int type) const
    {
        static int rd, cd, d;
        rd = locDest.row - rPos;
        cd = locDest.col - cPos;

        // Euclidian Distance
        if(type== 0)
        d = static_cast<int>(sqrt((double)(rd*rd+cd*cd)));

        // Manhattan distance
        else if(type== 1)
        d = abs(rd) + abs(cd);

        // Chebyshev distance
        else if(type== 2)
        d = max(abs(rd), abs(cd));

        return(d);
    }

	// Determine FValue (in the priority queue)
	friend bool operator<(const Node & a, const Node & b)
	{
	    return a.getFValue() > b.getFValue();
	}
};

class PathFinder
{
    public:
    PathFinder(NPC *npc);
    ~PathFinder();

    int width,height,start_x,start_y,finish_x,finish_y,directions,radius,type;
    std::string path = "";
    NPC *npc;

    int PathFinderMain(int sx,int sy,int ex,int ey);
    std::string FindPath(const Location &locStart ,const Location &locFinish);
    void ReducePath();
};

#endif // PATHFINDER_HPP_INCLUDED
