#include <iostream>
#include <Eigen/Dense>
#include <queue>
#include <vector>
#include <algorithm>
#include <pcl/octree/octree_search.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>

using Corridor = std::pair<Eigen::Vector3d, double>;
using namespace pcl;
namespace CorridorGen
{
    class CorridorGenerator
    {
    public:
    public: // public member variable
        // flight_corridor_[i].first is the position of i^th spherical corridor centre
        // flight_corridor_[i].second is the radius of i^th spherical corridor
        std::vector<Corridor> flight_corridor_;

    private: // private member variable
        Eigen::Vector3d initial_position_, goal_position_, local_guide_point_;
        double resolution_; // point cloud resolution
        double clearance_; // drone radius

        // global guide path
        std::vector<Eigen::Vector3d> guide_path_;

        // store the local point cloud
        pcl::octree::OctreePointCloudSearch<pcl::PointXYZ> octree_ =
            decltype(octree_)(0.1);

        // OctreePointCloudPointVector<PointXYZ> octree_(0.1);
        // pcl::octree::OctreePointCloudSearch<pcl::PointXYZ> octree_;

    public: // public member function
        CorridorGenerator(double resolution, double clearance);
        ~CorridorGenerator() = default;

        void updatePointCloud(const pcl::PointCloud<pcl::PointXYZ>::Ptr &new_cloud); // yes

        // Generate a sphere SFC given a position
        Corridor GenerateOneSphere(const Eigen::Vector3d &pos);

        void updateGlobalPath(const std::vector<Eigen::Vector3d> &path); // yes
        const std::vector<Corridor>& getCorridor() const;
        void generateCorridorAlongPath(const std::vector<Eigen::Vector3d> &path); // wip

    private: // private member function
        Eigen::Vector3d getGuidePoint(std::vector<Eigen::Vector3d> &guide_path, const Corridor &input_corridor);
        bool pointInCorridor(const Eigen::Vector3d &point, const Corridor &corridor);
        Corridor batchSample(const Eigen::Vector3d &guide_point, const Corridor &input_corridor);
        
        

        // more functions to implement batchSample(...)
    };

}; // namespace CorridorGenerator
