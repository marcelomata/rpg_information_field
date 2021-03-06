#pragma once

#include <string>
#include <vector>
#include <iostream>
#include <memory>

#include <vi_utils/vi_measurements.h>
#include <vi_utils/states.h>
#include <vi_utils/map.h>
#include <rpg_common/pose.h>

namespace vi_utils
{
class PinholeCam
{
public:
  EIGEN_MAKE_ALIGNED_OPERATOR_NEW

  static const std::string kGeoHeader;

  PinholeCam() = delete;
  PinholeCam(const std::vector<double>& geo_params_vec, const rpg::Pose& Tbc);

  static std::shared_ptr<PinholeCam>
  loadFromFile(const std::string& abs_cam_geo, const std::string& abs_Tbc);
  static std::shared_ptr<PinholeCam> loadFromDir(const std::string& dir);
  static std::shared_ptr<PinholeCam> createTestCam();

  void saveToFile(const std::string& abs_cam_geo,
                  const std::string& abs_Tbc) const;

  friend std::ostream& operator<<(std::ostream& os, const PinholeCam& pc);

  // accessors
  double fx() const
  {
    return fx_;
  }
  double fy() const
  {
    return fy_;
  }
  double cx() const
  {
    return cx_;
  }
  double cy() const
  {
    return cy_;
  }
  double w() const
  {
    return w_;
  }
  double h() const
  {
    return h_;
  }
  template <typename T>
  T wAs() const
  {
    return static_cast<T>(w_);
  }
  template <typename T>
  T hAs() const
  {
    return static_cast<T>(h_);
  }
  rpg::Pose Tbc() const
  {
    return T_b_c_;
  }
  Eigen::Matrix3d K() const
  {
    return K_;
  }

  // projection
  bool project3d(const Eigen::Vector3d& p_c, Eigen::Vector2d* u) const;
  template <int Cols>
  void project3dBatch(const Eigen::Matrix<double, 3, Cols>& pcs,
                      Eigen::Matrix<double, 2, Cols>* us,
                      std::vector<bool>* is_visible) const;
  template <int Cols>
  void project3dBatchWithIds(const Eigen::Matrix<double, 3, Cols>& pcs,
                             const Eigen::Matrix<int, 1, Cols>& ids,
                             CamMeasurements* cam_meas) const;

  // backprojection
  inline void backproject3d(const Eigen::Vector2d& u, Eigen::Vector3d* f) const
  {
    const Eigen::Vector3d homo_u(u(0), u(1), 1.0);
    (*f) = K_inv_ * homo_u;
  }

  template <int Cols>
  void backproject3dBatch(const Eigen::Matrix<double, 2, Cols>& us,
                          Eigen::Matrix<double, 3, Cols>* fs) const;

  // utilities
  inline bool isInsideImage(const Eigen::Vector2d& u) const
  {
    const double x = u(0);
    const double y = u(1);
    return ((x > w_margin_) && x < (static_cast<double>(w_) - w_margin_)) &&
           ((y > h_margin_) && y < (static_cast<double>(h_) - h_margin_));
  }

  inline bool isDepthValid(const Eigen::Vector3d& pc,
                           const double z_margin = 0.05) const
  {
    const double z = pc(2);
    return z > z_margin && z < max_depth_ && z > min_depth_;
  }

  inline bool isDistanceValid(const Eigen::Vector3d& pc) const
  {
    const double dist = pc.norm();
    return dist < max_dist_ && dist > min_dist_;
  }

  inline void setDepthRange(const double min_z, const double max_z)
  {
    CHECK_LT(min_z, max_z);
    min_depth_ = min_z;
    max_depth_ = max_z;
  }

  inline void setDistRange(const double min_dist, const double max_dist)
  {
    CHECK_LT(min_dist, max_dist);
    min_dist_ = min_dist;
    max_dist_ = max_dist;
  }

  inline double getMinDist() const
  {
    return min_dist_;
  }

  inline double getMaxDist() const
  {
    return max_dist_;
  }

  inline void setMargin(const double ratio)
  {
    CHECK_GE(ratio, 0.0);
    w_margin_ = w_ * ratio;
    h_margin_ = h_ * ratio;
  }

  inline int pixelCoordToFlatIdx(const int x, const int y) const
  {
    return y * w_ + x;
  }

  inline void getBearingAtPixel(const int x, const int y,
                                Eigen::Vector3d* f) const
  {
    CHECK(pixel_bearing_computed_);
    (*f) = bearings_at_pixels_.col(pixelCoordToFlatIdx(x, y));
  }

  inline const Eigen::Ref<const Eigen::Vector3d> getBearingAtPixel(
      const int x, const int y) const
  {
    CHECK(pixel_bearing_computed_);
    return bearings_at_pixels_.col(pixelCoordToFlatIdx(x, y));
  }

  inline int numBearings() const
  {
    CHECK(pixel_bearing_computed_);
    return bearings_at_pixels_.cols();
  }

  void computeBearingVectors();
  inline bool bearingVectorsComputed() const
  {
    return pixel_bearing_computed_;
  }

  static const std::string kGeo;
  static const std::string kTbc;
  static const std::string kExt;

private:
  void updateK();
  double fx_, fy_, cx_, cy_;
  int w_, h_;
  double w_margin_, h_margin_;
  double min_depth_ = -1;
  double max_depth_ = std::numeric_limits<double>::infinity();
  double min_dist_ = -1;
  double max_dist_ = std::numeric_limits<double>::infinity();
  rpg::Pose T_b_c_;
  Eigen::Matrix3d K_;
  Eigen::Matrix3d K_inv_;

  Eigen::Matrix3Xd bearings_at_pixels_;
  bool pixel_bearing_computed_ = false;
};
using PinholeCamPtr = std::shared_ptr<PinholeCam>;
using PinholeCamVec = std::vector<PinholeCamPtr>;

// templated: N can be dynamic or fixed
template <int Cols>
void PinholeCam::project3dBatch(const Eigen::Matrix<double, 3, Cols>& pcs,
                                Eigen::Matrix<double, 2, Cols>* us,
                                std::vector<bool>* is_visible) const
{
  CHECK_NOTNULL(us);
  CHECK_NOTNULL(is_visible);
  CHECK_EQ(pcs.cols(), us->cols());
  CHECK_EQ(pcs.cols(), static_cast<int>(is_visible->size()));
  const int N = pcs.cols();

  Eigen::Matrix<double, 3, Cols> us_homo;
  if (Cols == Eigen::Dynamic)
  {
    us_homo.resize(Eigen::NoChange, N);
  }
  us_homo = K_ * pcs;

  us->row(0) = us_homo.row(0).cwiseQuotient(us_homo.row(2));
  us->row(1) = us_homo.row(1).cwiseQuotient(us_homo.row(2));

  for (int i = 0; i < N; i++)
  {
    (*is_visible)[i] = isDepthValid(pcs.col(i)) && isInsideImage(us->col(i));
  }
}

template <int Cols>
void PinholeCam::backproject3dBatch(const Eigen::Matrix<double, 2, Cols>& us,
                                    Eigen::Matrix<double, 3, Cols>* fs) const
{
  CHECK_NOTNULL(fs);
  CHECK_EQ(us.cols(), fs->cols());
  const int N = us.cols();

  Eigen::Matrix<double, 3, Cols> us_homo;
  if (Cols == Eigen::Dynamic)
  {
    us_homo.resize(Eigen::NoChange, N);
    if (fs->cols() != N)
    {
      fs->resize(Eigen::NoChange, N);
    }
  }
  us_homo.block(0, 0, 2, N) = us;
  us_homo.row(2).setConstant(1.0);

  (*fs) = K_inv_ * us_homo;
}

template <int Cols>
void PinholeCam::project3dBatchWithIds(
    const Eigen::Matrix<double, 3, Cols>& pcs,
    const Eigen::Matrix<int, 1, Cols>& ids, CamMeasurements* cam_meas) const
{
  CHECK_NOTNULL(cam_meas);
  const int N = pcs.cols();
  Eigen::Matrix<double, 2, Cols> us;
  if (Cols == Eigen::Dynamic)
  {
    us.resize(Eigen::NoChange, N);
  }
  std::vector<bool> is_visible(N);
  project3dBatch(pcs, &us, &is_visible);

  int n_visible = std::count(is_visible.begin(), is_visible.end(), true);
  Eigen::MatrixXd vis_us(2, n_visible);
  std::vector<int32_t> vis_global_ids(n_visible);
  std::vector<int32_t> vis_track_ids(n_visible, -1);  // not used

  int vis_cnt = 0;
  for (int i = 0; i < N; i++)
  {
    if (is_visible[i])
    {
      vis_us.col(vis_cnt) = us.col(i);
      vis_global_ids[vis_cnt] = ids(0, i);
      vis_cnt++;
    }
  }
  cam_meas->setMeasurements(vis_us, vis_global_ids, vis_track_ids);
}
}  // namespace vi_utils

namespace vi_utils
{
class NCamera
{
public:
  static void projectBatchWithIds(const vi_utils::StatesVec& states_vec,
                                  const vi_utils::PinholeCamVec& cam_vec,
                                  const vi_utils::Map& map,
                                  KFCamMeasurementsVec* states_meas);
  static int numOfCameras(const std::string& abs_dir);

  static void loadCamerasFromDir(const std::string& load_dir,
                                 vi_utils::PinholeCamVec* cams);
};
}  // namespace vi_utils
namespace vi
{
using PinholeCam = vi_utils::PinholeCam;
using PinholeCamPtr = vi_utils::PinholeCamPtr;
using PinholeCamVec = vi_utils::PinholeCamVec;
using NCamera = vi_utils::NCamera;
}  // namespace vi
