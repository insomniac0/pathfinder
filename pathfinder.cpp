#include "character.hpp"
#include "console.hpp"
#include "map.hpp"
#include "npc.hpp"
#include "util.hpp"
#include "pathfinder.hpp"
#include "world.hpp"


Node::Node(const Location &loc, int g, int f)
{
    rPos = loc.row;
    cPos = loc.col;
    GValue = g;
    FValue = f;
}

PathFinder::PathFinder(NPC *npc)
{
    this->width = 0;
    this->height = 0;
    this->path = "";
    this->type = util::rand(0,2);
    this->npc = npc;
    this->directions = DIRECTIONS;// or 8 for diag   NDIR
    this->start_x = 0;
    this->start_y = 0;
    this->finish_x = 0;
    this->finish_y = 0;
    //putting radius to 0 basicly freezes server at this point ??
    this->radius = static_cast<int>(npc->map->world->config["PathFindingRadius"]);
}

void PathFinder::ReducePath()
{
    if(!this->npc)
    {
        Console::Err("Pathfind logic error invalid npc");
        return;
    }

    if(!this->npc->map->world->config["PathFinding"]) return;

    if(!this->path.empty() && npc->alive)
    {
        //0 right 1 down 2 left 3 up
        if(this->path[0] == '0')
        this->npc->direction = DIRECTION_RIGHT;
        else if(this->path[0] == '1')
        this->npc->direction = DIRECTION_DOWN;
        else if(this->path[0] == '2')
        this->npc->direction = DIRECTION_LEFT;
        else if(this->path[0] == '3')
        this->npc->direction = DIRECTION_UP;
        else
        {
            Console::Err("Pathfind logic error invalid direction %c",this->path[0]);
            this->path = "";
            return;
        }
        this->path.erase(0,1);
    }
}

// A-star algorithm.  2 left  0 right  3 up  1 down
// The path returned is a string of direction digits.
std::string PathFinder::FindPath( const Location &locStart ,const Location &locFinish)
{
    if(!this->npc->map->world->config["PathFinding"]) return "";

    static priority_queue<Node> q[2];
    static int qi = 0;
    static Node* node1,*node2 = 0;
    static int x, y, row, col, x_next, y_next;
    static char c;

    // reset the Node lists (0 = ".")
    for(int j = 0; j < MAX_HEIGHT; j++)//y
    {
        for(int i = 0; i < MAX_WIDTH; i++)//x
        {
            if(i < 0 || i >= MAX_WIDTH || j < 0 || j >=  MAX_HEIGHT)continue;

            closedNodes[i][j] = 0;
            openNodes[i][j] = 0;
        }
    }

    // create the start node and push into list of open nodes
    node1 = new Node(locStart, 0, 0);
    node1->calculateFValue(locFinish,type);
    q[qi].push(*node1);

    delete node1;//test remove mem leak
    // A* search
    while(!q[qi].empty())
    {
        // get the current node w/ the lowest FValue
        // from the list of open nodes
        node1 = new Node( q[qi].top().getLocation(),q[qi].top().getGValue(), q[qi].top().getFValue());

        row = (node1->getLocation()).row;
	    col = node1->getLocation().col;
	    //cout << "row, col=" << row << "," << col << endl;

	    // remove the node from the open list
        q[qi].pop();
        openNodes[row][col] = 0;

        // mark it on the closed nodes list
        closedNodes[row][col] = 1;

        // stop searching when the goal state is reached
        if(row == locFinish.row && col == locFinish.col)
        {
	        // generate the path from finish to start from dirMap
            std::string path = "";
            while(!(row == locStart.row && col == locStart.col))
            {
                y = dirMap[row][col];
                c = '0' + (y + directions/2) % directions;
                path = c + path;
                row += xdir[y];
                col += ydir[y];
            }
            // garbage collection
            delete node1;
            // empty the leftover nodes
            while(!q[qi].empty())
            {
                q[qi].pop();
            }
            return path;
        }
        // generate moves in all possible directions
        for(x = 0; x < directions; x++)
        {
            x_next = row + xdir[x];
	        y_next = col + ydir[x];
	        // if not wall (obstacle) nor in the closed list
            if(!(x_next < 0 || x_next > width - 1 || y_next < 0 || y_next > height - 1 ||
            squares[x_next][y_next] == 1 || closedNodes[x_next][y_next] == 1))
            {
		        // generate a child node
                node2 = new Node(Location(x_next,y_next), node1->getGValue(), node1->getFValue());
                node2->updateGValue(x);
                node2->calculateFValue(locFinish,type);
                // if it is not in the open list then add into that
                if(openNodes[x_next][y_next] == 0)
                {
                    openNodes[x_next][y_next] = node2->getFValue();
                    q[qi].push(*node2);
                    // mark its parent node direction
                    dirMap[x_next][y_next] = (x + directions/2) % directions;
                }
		        // already in the open list
                else if(openNodes[x_next][y_next] > node2->getFValue())
                {
                    // update the FValue info
                    openNodes[x_next][y_next] = node2->getFValue();
                    // update the parent direction info,  mark its parent node direction
                    dirMap[x_next][y_next] = (x + directions/2) % directions;
                    // replace the node by emptying one q to the other one
                    // except the node to be replaced will be ignored
                    // and the new node will be pushed in instead
                    while(!(q[qi].top().getLocation().row == x_next &&
                    q[qi].top().getLocation().col == y_next))
                    {
                        q[1 - qi].push(q[qi].top());
                        q[qi].pop();
                    }
		            // remove the wanted node
                    q[qi].pop();
                    // empty the larger size q to the smaller one
                    if(q[qi].size() > q[1 - qi].size()) qi = 1 - qi;
                    while(!q[qi].empty())
                    {
                        q[1 - qi].push(q[qi].top());
                        q[qi].pop();
                    }
                    qi = 1 - qi;
		            // add the better node instead
                    q[qi].push(*node2);
                }
                delete node2;
            }
        }
        delete node1;
    }

    // no path found
    return "";
}

int PathFinder::PathFinderMain(int sx,int sy,int ex,int ey)
{
    if(!this->npc->map->world->config["PathFinding"]) return 1;

    this->height = npc->y+radius;
    this->width = npc->x+radius;

    for(int x = width -(radius *2); x < width; x++)//x
    {
        for(int y = height-(radius *2); y < height; y++)//y
        {
            if(x < 0 || x >= MAX_WIDTH-1 || y < 0 || y > MAX_HEIGHT-1)continue;

            bool occupied = false;
            UTIL_FOREACH(npc->map->characters, character)
            {
                if (character->x == x && character->y == y && x != sx && y != sy && x != ex && y != ey)
                {
                    occupied = true;
                    break;
                }
            }

            if(!occupied)
            {
                UTIL_FOREACH(npc->map->npcs, n)
                {
                    if (n != npc && n->alive && n->x == x && n->y == y && x != sx && y != sy && x != ex && y != ey)
                    {
                        occupied = true;
                        break;
                    }
                }
            }

            if ((npc->map->Walkable(x,y, true) || npc->NoWall(x,y)) && !occupied)
                squares[x][y] = 0; //make path
            else
                squares[x][y] = 1; // make wall
        }
    }

    this->path = FindPath(Location(sx,sy), Location(ex,ey));

    return 0;
}

PathFinder::~PathFinder()
{
    delete this;
}
