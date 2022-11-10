#include <plan_env/grid_map.h>
#include <ros/ros.h>
#include <iostream>
#include <Eigen/Dense>
#include <geometry_msgs/PoseStamped.h>
#include <visualization_msgs/MarkerArray.h>
enum ASTAR_RET
{
  SUCCESS,
  INIT_ERR,
  SEARCH_ERR
};

struct GridNode;
constexpr double inf = 1 >> 20;
typedef GridNode *GridNodePtr;
struct GridNode
{
  Eigen::Vector3i index; // grid map index
  GridNode *parent{nullptr};
  enum node_state
  {
    OPENLIST = 1,
    CLOSELIST = 2,
    UNEXPLORED = 3,
  };

  enum node_state state
  {
    UNEXPLORED
  };

  double rounds{0};

  double cost_so_far{inf};          // running cost
  double total_cost_estimated{inf}; // estimate cost to go, must be admissible
                                    // i.e. means it should be less than actual cost to goal
};

class NodeComparator // such that node with lower cost (closer to goal) will be infront
{
public:
  bool operator()(GridNodePtr node1, GridNodePtr node2)
  {
    return node1->total_cost_estimated > node2->total_cost_estimated;
  }
};

// global variable
Eigen::Vector3d current_pos;
Eigen::Vector3d target_pos;
GridNodePtr ***GridNodeMap_;
Eigen::Vector3i CENTER_IDX_, POOL_SIZE_;
GridMap::Ptr astar_grid_map_; // fact is that this is only used for checking occupancy.
// this means that any mapping modules that takes in a position query should work with the search module
double step_size_;
double inv_step_size_;
Eigen::Vector3d center_; // center point of start and end positions
bool require_planning = false;
std::priority_queue<GridNodePtr, std::vector<GridNodePtr>, NodeComparator> openSet_;
const double tie_breaker_ = 1.0 + 1.0 / 10000;
int rounds_{0};
std::vector<GridNodePtr> gridPath_;

void poseCallback(const geometry_msgs::PoseStampedConstPtr &msg)
{
  current_pos.x() = msg->pose.position.x;
  current_pos.y() = msg->pose.position.y;
  current_pos.z() = msg->pose.position.z;
}

void targetCallback(const geometry_msgs::PoseStampedConstPtr &msg)
{
  target_pos.x() = msg->pose.position.x;
  target_pos.y() = msg->pose.position.y;
  target_pos.z() = msg->pose.position.z;

  std::cout << "Goal received: " << target_pos.transpose() << std::endl;
  require_planning = true;
}

void initGridMap(GridMap::Ptr grid_map, const Eigen::Vector3i &map_size);

ASTAR_RET AStarSearch(const double step_size, const Eigen::Vector3d &start, const Eigen::Vector3d &goal, GridNodePtr &ret_node);

bool ConvertToIndexAndAdjustStartEndPoints(Eigen::Vector3d start_pt, Eigen::Vector3d end_pt, Eigen::Vector3i &start_idx, Eigen::Vector3i &end_idx);

bool Coord2Index(const Eigen::Vector3d &pt, Eigen::Vector3i &idx);

Eigen::Vector3d Index2Coord(const Eigen::Vector3i &index);

int checkOccupancy(const Eigen::Vector3d &pos);

double getDiagHeu(GridNodePtr node1, GridNodePtr node2);

double getHeu(GridNodePtr node1, GridNodePtr node2);

void retrivePath(const GridNodePtr &ret_node, std::vector<Eigen::Vector3d> &path_list);

std::vector<GridNodePtr> retrivePath(GridNodePtr ret_node);

int main(int argc, char **argv)
{

  ros::init(argc, argv, "occupancy_mapping");
  ros::NodeHandle nh("~");

  // calling on GridMap class to generate inflated occupancy map
  ros::Time time_1 = ros::Time::now();
  GridMap::Ptr grid_map_;
  grid_map_.reset(new GridMap);
  grid_map_->initMap(nh);

  ros::Duration(1.0).sleep();

  ros::Time time_2 = ros::Time::now();

  // initialize gridmap for A* search
  int map_size_;
  nh.getParam("astar_map_size", map_size_);
  std::cout << "map_size is " << map_size_ << std::endl;
  Eigen::Vector3i map_size{map_size_, map_size_, map_size_}; // what is the resolution of the map? 0.1m should be same as
  initGridMap(grid_map_, map_size);
  ros::Time time_3 = ros::Time::now();

  std::cout << "grid map init used: " << (time_2 - time_1).toSec() << std::endl;
  std::cout << "astar GridNodeMap_ init used: " << (time_3 - time_2).toSec() << std::endl;

  double step_size = grid_map_->getResolution();

  ros::Subscriber pose_nwu_sub_ = nh.subscribe("/drone0/global_nwu", 10, poseCallback);
  ros::Subscriber target_nwu_sub_ = nh.subscribe("/goal", 10, targetCallback);
  ros::Publisher vis_pub = nh.advertise<visualization_msgs::MarkerArray>("Astar_node", 0);
  // ros::spin();

  ros::Rate rate(20);
  GridNodePtr ret_node;
  std::vector<Eigen::Vector3d> path_list;
  while (ros::ok())
  {
    if (require_planning)
    {
      if((target_pos - current_pos).norm() < 0.1)
      {
        require_planning = false;
      }
      // initGridMap(grid_map_, map_size);
      std::cout << "required planning" << std::endl;
      ros::Time time_1 = ros::Time::now();
      ASTAR_RET ret = AStarSearch(step_size, current_pos, target_pos, ret_node);
      ros::Time time_2 = ros::Time::now();

      // require_planning = false;
      visualization_msgs::MarkerArray astar_nodes;

      if (ret == ASTAR_RET::SUCCESS)
      {
        // std::cout << "ret_node position is " << Index2Coord(ret_node->index) << std::endl;
        std::cout << " Search Success " << std::endl;
        std::cout << "Used: " << (time_2 - time_1).toSec() << " seconds" << std::endl;
        path_list.clear();
        retrivePath(ret_node, path_list);
        std::cout << "Path retrived" << std::endl;
        visualization_msgs::MarkerArray astar_nodes;
        int array_size = path_list.size();
        astar_nodes.markers.clear();
        astar_nodes.markers.resize(array_size);
        // path_list.clear();
        // path_list.emplace_back(current_pos);
        // path_list.emplace_back(target_pos);

        for (int i = 0; i < array_size; i++)
        {
          visualization_msgs::Marker node;
          node.header.frame_id = "map";
          node.header.stamp = ros::Time::now();
          node.id = i;
          node.type = visualization_msgs::Marker::SPHERE;
          node.action = visualization_msgs::Marker::ADD;
          node.pose.position.x = path_list[i].x();
          node.pose.position.y = path_list[i].y();
          node.pose.position.z = path_list[i].z();
          node.pose.orientation.x = 0.0;
          node.pose.orientation.y = 0.0;
          node.pose.orientation.z = 0.0;
          node.pose.orientation.w = 1.0;
          node.scale.x = 0.1;
          node.scale.y = 0.1;
          node.scale.z = 0.1;
          node.color.a = 1.0; // Don't forget to set the alpha!
          node.color.r = 0.0;
          node.color.g = 1.0;
          node.color.b = 0.0;
          astar_nodes.markers.emplace_back(node);
        }
        vis_pub.publish(astar_nodes);
      }

      else if (ret == ASTAR_RET::SEARCH_ERR)
      {
        std::cout << " Search Error " << std::endl;
      }
    }
    ros::spinOnce();
    rate.sleep();
  }

  return 0;
}

void initGridMap(GridMap::Ptr grid_map, const Eigen::Vector3i &map_size)
{
  POOL_SIZE_ = map_size;
  CENTER_IDX_ = map_size / 2;

  GridNodeMap_ = new GridNodePtr **[POOL_SIZE_(0)];
  for (int i = 0; i < POOL_SIZE_(0); i++)
  {
    GridNodeMap_[i] = new GridNodePtr *[POOL_SIZE_(1)];
    for (int j = 0; j < POOL_SIZE_(1); j++)
    {
      GridNodeMap_[i][j] = new GridNodePtr[POOL_SIZE_(2)];
      for (int k = 0; k < POOL_SIZE_(2); k++)
      {
        GridNodeMap_[i][j][k] = new GridNode;
      }
    }
  }

  astar_grid_map_ = grid_map;
}

ASTAR_RET AStarSearch(const double step_size, const Eigen::Vector3d &start, const Eigen::Vector3d &goal, GridNodePtr &ret_node)
{
  ros::Time time_1 = ros::Time::now();
  ++rounds_;
  step_size_ = step_size;
  inv_step_size_ = 1 / step_size;

  // Check if goal is out of sensing range
  Eigen::Vector3d temp_goal;
  if ((goal - start).norm() > 10)
  {
    ROS_WARN("Goal is outside sensing range, will need to adjust");
    temp_goal = start + (goal - start) / (goal - start).norm() * 9.5;
  }

  else
  {
    temp_goal = goal;
  }
  center_ = (start + temp_goal) / 2;

  Eigen::Vector3i start_index, goal_index;

  // std::cout << "start_index_temp " << start_index_temp.transpose() << std::endl;
  // std::cout << "goal_index_temp " << goal_index_temp.transpose() << std::endl;
  ros::Time adjust_start = ros::Time::now();
  if (!ConvertToIndexAndAdjustStartEndPoints(start, temp_goal, start_index, goal_index))
  {
    ROS_ERROR("Unable to handle the initial or end point, force return!");
    return ASTAR_RET::INIT_ERR;
  }
  ros::Time adjust_end = ros::Time::now();
  std::cout << " adjusting takes " << (adjust_end - adjust_start).toSec() << std::endl;

  GridNodePtr startPtr = GridNodeMap_[start_index(0)][start_index(1)][start_index(2)];
  GridNodePtr endPtr = GridNodeMap_[goal_index(0)][goal_index(1)][goal_index(2)];

  std::priority_queue<GridNodePtr, std::vector<GridNodePtr>, NodeComparator> empty;
  openSet_.swap(empty);
  GridNodePtr neighborPtr = nullptr;
  GridNodePtr current = nullptr;
  endPtr->index = goal_index;

  startPtr->index = start_index;
  startPtr->cost_so_far = 0;
  startPtr->rounds = rounds_;
  startPtr->total_cost_estimated = getHeu(startPtr, endPtr);
  startPtr->state = GridNode::OPENLIST;
  startPtr->parent = nullptr;
  openSet_.push(startPtr);

  double tentative_gScore;
  int num_iter = 0;

  while (!openSet_.empty())
  {
    num_iter++;
    current = openSet_.top();
    // std::cout << "current popped is " << current << std::endl;
    // std::cout << "current neighbour are " << std::endl;
    openSet_.pop();
    if (current->index(0) == endPtr->index(0) && current->index(1) == endPtr->index(1) && current->index(2) == endPtr->index(2))
    {
      ros::Time time_2 = ros::Time::now();
      std::cout << "Found path, time used is " << (time_2 - time_1).toSec() << std::endl;
      // printf("\033[34mA star iter:%d, time:%.3f\033[0m\n",num_iter, (time_2 - time_1).toSec()*1000);
      // if((time_2 - time_1).toSec() > 0.1)
      //     ROS_WARN("Time consume in A star path finding is %f", (time_2 - time_1).toSec() );
      ret_node = current;
      // temp = temp->parent;
      return ASTAR_RET::SUCCESS;
    }

    current->state = GridNode::CLOSELIST;

    for (int dx = -1; dx <= 1; dx++)
      for (int dy = -1; dy <= 1; dy++)
        for (int dz = -1; dz <= 1; dz++)
        {
          if (dx == 0 && dy == 0 && dz == 0) // ego node
            continue;

          Eigen::Vector3i neighborIdx;
          // neighborIdx = current->index + Eigen::Vector3i(dx, dy, dz);

          neighborIdx(0) = (current->index)(0) + dx;
          neighborIdx(1) = (current->index)(1) + dy;
          neighborIdx(2) = (current->index)(2) + dz;

          if (neighborIdx(0) < 1 || neighborIdx(0) >= POOL_SIZE_(0) - 1 || neighborIdx(1) < 1 || neighborIdx(1) >= POOL_SIZE_(1) - 1 || neighborIdx(2) < 1 || neighborIdx(2) >= POOL_SIZE_(2) - 1)
          {
            continue;
          }

          neighborPtr = GridNodeMap_[neighborIdx(0)][neighborIdx(1)][neighborIdx(2)]; // just to store the node in the memory address
          // std::cout << neighborPtr << std::endl;
          neighborPtr->index = neighborIdx;

          bool explored = neighborPtr->rounds == rounds_;
          if (explored && neighborPtr->state == GridNode::CLOSELIST)
          {
            continue; // we have explored this neighbor and it has also been expanded
          }

          neighborPtr->rounds = rounds_; // this is important because rounds_ increment for every call of A* round
                                         // GridNodeMap_ coordinates only make sense for current call since it depends on
                                         // the start and end node position
                                         // as the center position has index 50,50,50 is fixed

          // check if neighborPtr is occupied
          if (checkOccupancy(Index2Coord(neighborPtr->index)))
          {
            continue;
          }

          double static_cost = sqrt(dx * dx + dy * dy + dz * dz);
          tentative_gScore = current->cost_so_far + static_cost;

          // if (neighborPtr->state == GridNode::UNEXPLORED) // this is the default state of any unexplored node
          if (!explored)
          {
            // this is a new node, we need to:
            // 1. update its cost_so_far
            // 2. add to openlist
            // 3. update its parent node
            // 4. push to priority queue
            //
            neighborPtr->cost_so_far = tentative_gScore;
            neighborPtr->total_cost_estimated = tentative_gScore + getHeu(neighborPtr, endPtr);
            neighborPtr->parent = current;
            neighborPtr->state = GridNode::OPENLIST;
            openSet_.push(neighborPtr);
            // std::cout << "neightbour node: " << neighborPtr << " has parent: "<< neighborPtr->parent << std::endl;
          }

          else if (tentative_gScore < neighborPtr->cost_so_far)
          {
            // update the node with a shorter parent
            neighborPtr->parent = current;
            // std::cout << "neightbour node: " << neighborPtr <<  " has parent: " << neighborPtr->parent << std::endl;
            neighborPtr->cost_so_far = tentative_gScore;
            neighborPtr->total_cost_estimated = tentative_gScore + getHeu(neighborPtr, endPtr);
          }
        }
    ros::Time time_2 = ros::Time::now();
    if ((time_2 - time_1).toSec() > 0.2)
    {
      // ROS_WARN("Failed in A star path searching !!! 0.2 seconds time limit exceeded.");
      // return ASTAR_RET::SEARCH_ERR;
    }
  }

  ros::Time time_2 = ros::Time::now();

  if ((time_2 - time_1).toSec() > 0.1)
    ROS_WARN("Time consume in A star path finding is %.3fs, iter=%d", (time_2 - time_1).toSec(), num_iter);

  return ASTAR_RET::SEARCH_ERR;
}

bool ConvertToIndexAndAdjustStartEndPoints(Eigen::Vector3d start_pt, Eigen::Vector3d end_pt, Eigen::Vector3i &start_idx, Eigen::Vector3i &end_idx)
{
  std::cout << "start point: " << start_pt.transpose() << std::endl;
  std::cout << "end_pt: " << end_pt.transpose() << std::endl;
  if (!Coord2Index(start_pt, start_idx) || !Coord2Index(end_pt, end_idx))
    return false;

  int occ;
  if (checkOccupancy(Index2Coord(start_idx)))
  {
    ROS_WARN("Start point is insdide an obstacle.");
    do
    {
      start_pt = (start_pt - end_pt).normalized() * step_size_ + start_pt; // vector from end to start
      // cout << "start_pt=" << start_pt.transpose() << endl;
      if (!Coord2Index(start_pt, start_idx))
      {
        return false;
      }

      occ = checkOccupancy(Index2Coord(start_idx));
      if (occ == -1)
      {
        ROS_WARN("[Astar] Start point outside the map region.");
        return false;
      }
    } while (occ);
  }

  if (checkOccupancy(Index2Coord(end_idx)))
  {
    ROS_WARN("End point is insdide an obstacle.");
    do
    {
      end_pt = (end_pt - start_pt).normalized() * step_size_ + end_pt; // vector from start to end
      // cout << "end_pt=" << end_pt.transpose() << endl;
      if (!Coord2Index(end_pt, end_idx))
      {
        return false;
      }

      occ = checkOccupancy(Index2Coord(start_idx));
      if (occ == -1)
      {
        ROS_WARN("[Astar] End point outside the map region.");
        return false;
      }
    } while (checkOccupancy(Index2Coord(end_idx)));
  }

  return true;
}

bool Coord2Index(const Eigen::Vector3d &pt, Eigen::Vector3i &idx)
{
  std::cout << "Position " << pt.transpose() << std::endl;
  idx = ((pt - center_) * inv_step_size_ + Eigen::Vector3d(0.5, 0.5, 0.5)).cast<int>() + CENTER_IDX_;

  // if (idx(0) < 0 || idx(0) >= POOL_SIZE_(0) || idx(1) < 0 || idx(1) >= POOL_SIZE_(1) || idx(2) < 0 || idx(2) >= POOL_SIZE_(2))
  // {
  //   ROS_ERROR("center in dex is =%d %d %d", CENTER_IDX_(0), CENTER_IDX_(1), CENTER_IDX_(2));
  //   ROS_ERROR("Ran out of pool, index=%d %d %d", idx(0), idx(1), idx(2));
  //   ROS_ERROR("Position %d %d %d", pt(0), pt(1), pt(2));
  //   return false;
  // }

  if (idx(0) < 0)
  {
    idx(0) = 1;
  }

  if (idx(1) < 0)
  {
    idx(1) = 1;
  }

  if (idx(2) < 0)
  {
    idx(2) = 1;
  }

  if (idx(0) >= POOL_SIZE_(0))
  {
    idx(0) = POOL_SIZE_(0) - 1;
  }

  if (idx(1) >= POOL_SIZE_(1))
  {
    idx(0) = POOL_SIZE_(1) - 1;
  }

  if (idx(2) >= POOL_SIZE_(2))
  {
    idx(0) = POOL_SIZE_(2) - 1;
  }
  return true;
}

Eigen::Vector3d Index2Coord(const Eigen::Vector3i &index)
{
  return ((index - CENTER_IDX_).cast<double>() * step_size_) + center_;
}

int checkOccupancy(const Eigen::Vector3d &pos) { return astar_grid_map_->getInflateOccupancy(pos); }

double getDiagHeu(GridNodePtr node1, GridNodePtr node2)
{
  double dx = abs(node1->index(0) - node2->index(0));
  double dy = abs(node1->index(1) - node2->index(1));
  double dz = abs(node1->index(2) - node2->index(2));

  double h = 0.0;
  int diag = min(min(dx, dy), dz);
  dx -= diag;
  dy -= diag;
  dz -= diag;

  if (dx == 0)
  {
    h = 1.0 * sqrt(3.0) * diag + sqrt(2.0) * min(dy, dz) + 1.0 * abs(dy - dz);
  }
  if (dy == 0)
  {
    h = 1.0 * sqrt(3.0) * diag + sqrt(2.0) * min(dx, dz) + 1.0 * abs(dx - dz);
  }
  if (dz == 0)
  {
    h = 1.0 * sqrt(3.0) * diag + sqrt(2.0) * min(dx, dy) + 1.0 * abs(dx - dy);
  }
  return h;
}

double getHeu(GridNodePtr node1, GridNodePtr node2)
{
  return tie_breaker_ * getDiagHeu(node1, node2);
}

void retrivePath(const GridNodePtr &ret_node, std::vector<Eigen::Vector3d> &path_list)
{
  GridNodePtr temp = ret_node;
  path_list.emplace_back(Index2Coord(temp->index));
  // while (temp->parent != nullptr && temp->parent->parent != nullptr)
  // {
  //   Eigen::Vector3d first = Index2Coord(temp->index);
  //   Eigen::Vector3d second = Index2Coord(temp->parent->index);
  //   Eigen::Vector3d third = Index2Coord(temp->parent->parent->index);

  //   double first_segment = (second - first).norm();
  //   double second_segment = (third - second).norm();
  //   double third_segment = (third - first).norm();

  //   if (first_segment + second_segment - third_segment < 0.01)
  //   {
  //     // colinear points
  //     temp->state = GridNode::UNEXPLORED;
  //     temp = temp->parent->parent;
  //     continue;
  //   }
  //   else
  //   {
  //     temp->state = GridNode::UNEXPLORED;
  //     temp = temp->parent;
  //     path_list.emplace_back(Index2Coord(temp->index));
  //   }

  //   // path_list.emplace_back(Index2Coord(temp->index));
  //   // temp->state = GridNode::UNEXPLORED;
  //   // temp = temp->parent;
  //   // std::cout << "node position" << Index2Coord(temp->index) << std::endl;
  // }
  // // path_list.emplace_back(Index2Coord(temp->parent->index));
  // if (temp->parent == nullptr)
  // {
  //   path_list.emplace_back(Index2Coord(temp->index));
  // }
  // else if (temp->parent->parent == nullptr)
  // {
  //   path_list.emplace_back(Index2Coord(temp->parent->index));
  // }

  // check colinear
  // GridNodePtr anchor = temp;
  // GridNodePtr front = temp->parent;
  // GridNodePtr back = front->parent;
  // // front, middle, back
  // // while(temp->parent != nullptr && temp->parent->parent != nullptr)
  // while (back->parent != nullptr)
  // {
  //   Eigen::Vector3d anchor_vect = Index2Coord(anchor->index);
  //   Eigen::Vector3d front_vect = Index2Coord(front->index);
  //   Eigen::Vector3d back_vect = Index2Coord(back->index);
  //   Eigen::Vector3d anchor2front = (front_vect - anchor_vect) / (front_vect - anchor_vect).norm();
  //   Eigen::Vector3d anchor2back = (back_vect - anchor_vect) / (back_vect - anchor_vect).norm();
  //   bool colinear;
  //   double angle = acos(anchor2front.dot(anchor2back));
  //   if (angle < 0.05) // almost colinear
  //   {
  //     front = front->parent;
  //     back = back->parent;
  //     continue;
  //   }

  //   else
  //   {
  //     path_list.emplace_back(Index2Coord(anchor->index));
  //     anchor = front;
  //     front = back;
  //     back = back->parent;
  //   }
  // }
  // path_list.emplace_back(Index2Coord(anchor->index));
  // path_list.emplace_back(Index2Coord(front->index));
  // path_list.emplace_back(Index2Coord(back->index));

  // // not considering colinear
  while (temp->parent != nullptr)
  {
    temp = temp->parent;
    path_list.emplace_back(Index2Coord(temp->index));
  }

  reverse(path_list.begin(), path_list.end());

  std::cout << "path_list waypts: " << path_list.size() << std::endl;
}

std::vector<GridNodePtr> retrivePath(GridNodePtr ret_node)
{
  std::vector<GridNodePtr> path;
  path.push_back(ret_node);
  int count = 0;
  std::cout << ret_node << std::endl;

  while (ret_node->parent != NULL)
  {
    if (count < 10)
    {
      std::cout << "ret_node is: " << ret_node << std::endl;
      std::cout << "ret_node parent is: " << ret_node->parent << std::endl;
      count++;
    }
    ret_node = ret_node->parent;

    path.push_back(ret_node);
    // std::cout << "node position is: " << Index2Coord(ret_node->index) << std::endl;
  }

  return path;
}
