#include <Eigen/Dense>
#include <vector>
#include <algorithm>
#include <ceres/ceres.h>
#include "implicit_cost_functions.h"
#include "implicit_intrinsic.h"
#include <numeric>
#include "implicit_utils.h" 

namespace colmap {
IntrinsicCalib calibrate_fix_lambda(const std::vector<Eigen::Vector2d> &points2D, 
                                const std::vector<Eigen::Vector3d> &points3D,
                                const CostMatrix &cost_matrix, const Eigen::Vector2d &pp,
                                const CameraPose &pose, double lambda) {
    return calibrate_fix_lambda_multi({points2D}, {points3D}, cost_matrix, pp, {pose}, lambda);
}


IntrinsicCalib calibrate_fix_lambda_multi_w_initial_guess(const std::vector<std::vector<Eigen::Vector2d>> &points2D, 
                                const std::vector<std::vector<Eigen::Vector3d>> &points3D,
                                const CostMatrix &cost_matrix, const Eigen::Vector2d &pp,
                                const std::vector<CameraPose> &poses, 
                                std::vector<std::vector<double>> &fvec, double lambda) {

    IntrinsicCalib calib;
    calib.pp = pp;

    ceres::Problem problem;
    ceres::LossFunction *loss_function_data = new ceres::HuberLoss(1e-4);

    // To track used parameter blocks and prevent duplicates
    std::set<double*> used_parameter_blocks;
    // std::set<std::vector<double*>> unique_parameter_blocks;


    // Compute pointwise focal lengths and add data cost
    size_t num_pts = 0;
    // bool initialize_fvec = fvec.size() == 0;
    // if (initialize_fvec) {
    //     fvec.resize(points2D.size());
    // }
    if (fvec.empty()) {
        fvec.resize(points2D.size());
        for (size_t i = 0; i < points2D.size(); ++i) {
            fvec[i].resize(points2D[i].size(), 0);
        }
    }
    for (size_t cam_ind = 0; cam_ind < points2D.size(); ++cam_ind) {
        // if (initialize_fvec) {
        //     fvec[cam_ind].resize(points2D[cam_ind].size());
        // }
        for (size_t pt_ind = 0; pt_ind < points2D[cam_ind].size(); ++pt_ind) {
            // Eigen::Vector2d z = points2D[cam_ind][pt_ind] - pp;
            // Eigen::Vector3d Z = poses[cam_ind].apply(points3D[cam_ind][pt_ind]);
            
            // double f = (z.squaredNorm() * Z[2]) / (Z.topRows<2>().dot(z));
            // if (f==0) {
            //     fvec[cam_ind][pt_ind] = f;
            // } 
            double& f = fvec[cam_ind][pt_ind];
            if (f == 0) { // Initialize if not yet initialized
            Eigen::Vector2d z = points2D[cam_ind][pt_ind] - pp;
            Eigen::Vector3d Z = poses[cam_ind].apply(points3D[cam_ind][pt_ind]);
            f = (z.squaredNorm() * Z[2]) / (Z.topRows<2>().dot(z));
            }
            ceres::CostFunction *data_term = FocalDataCost::CreateCost(f);
            // std::cout << "Adding residual for point " << pt_ind << " at address " << &(fvec[cam_ind][pt_ind]) << std::endl;
             if (used_parameter_blocks.insert(&(fvec[cam_ind][pt_ind])).second) {  // Only add if not already used
                problem.AddResidualBlock(data_term, loss_function_data, &(fvec[cam_ind][pt_ind]));
                num_pts++;
            }
            // problem.AddResidualBlock(data_term, loss_function_data, &(fvec[cam_ind][pt_ind]));
            // num_pts++;
        }
    }


    // Add regularization    
    ceres::LossFunction *scaled_loss = new ceres::ScaledLoss(new ceres::TrivialLoss(), lambda, ceres::TAKE_OWNERSHIP);
    std::vector<std::vector<double*>> params;
    params.resize(cost_matrix.pt_index.size());
    std::set<std::vector<double*>> unique_parameter_blocks;
    for (size_t i = 0; i < cost_matrix.pt_index.size(); ++i) {
        ceres::CostFunction* reg_cost = FocalCostMatrixRowCost::CreateCost(
                cost_matrix.pt_index[i], cost_matrix.cam_index[i], cost_matrix.values[i], fvec, params[i]);

        // std::cout << "Adding residual for cost matrix row " << i << std::endl;
        std::set<double*> local_unique_params;
        if (unique_parameter_blocks.insert(params[i]).second) {
            bool has_duplicate = false;
            for (size_t j = 0; j < cost_matrix.pt_index[i].size(); ++j) {
                if (!local_unique_params.insert(params[i][j]).second) {
                    std::cerr << "Warning: Duplicate parameter block detected and skipped at address: " << params[i][j] << std::endl;
                    has_duplicate = true;
                    break;
                }
                // std::cout << "Adding residual for point " << cost_matrix.pt_index[i][j] << " at address " << params[i][j] << std::endl;
                // problem.AddResidualBlock(reg_cost, scaled_loss, params[i]);
            }
            if(!has_duplicate) {
              problem.AddResidualBlock(reg_cost, scaled_loss, params[i]);
            }
            else {
              std::cerr << "Warning: Duplicate parameter block detected and skipped for cost matrix row " << i << std::endl;
            }
        }
        // problem.AddResidualBlock(reg_cost, scaled_loss, params[i]);
    }

    ceres::Solver::Options options; 
    options.max_num_iterations = 100;
    // options.linear_solver_type = ceres::SPARSE_NORMAL_CHOLESKY;
    options.linear_solver_type = ceres::SPARSE_SCHUR;
    options.minimizer_progress_to_stdout = false; // true if you want more debug output
    options.num_threads = 16;
    ceres::Solver::Summary summary;
    ceres::Solve(options, &problem, &summary);

    calib.r_f.reserve(num_pts);
    calib.theta_r.reserve(num_pts);
    for (size_t cam_ind = 0; cam_ind < points2D.size(); ++cam_ind) {
        for (size_t pt_ind = 0; pt_ind < points2D[cam_ind].size(); ++pt_ind) {            
            double r = (points2D[cam_ind][pt_ind] - pp).norm();
            double f = fvec[cam_ind][pt_ind];
            double theta = std::atan2(r, f);
            calib.r_f.emplace_back(r, f);
            calib.theta_r.emplace_back(theta, r);
        }
    }

    std::sort(calib.r_f.begin(), calib.r_f.end());
    std::sort(calib.theta_r.begin(), calib.theta_r.end());
    
    return calib;
}





// IntrinsicCalib  calibrate_fix_lambda_multi_w_initial_guess(
//     const std::vector<std::vector<Eigen::Vector2d>>& points2D, 
//     const std::vector<std::vector<Eigen::Vector3d>>& points3D,
//     const CostMatrix& cost_matrix, 
//     const Eigen::Vector2d& pp,
//     const std::vector<CameraPose>& poses, 
//     std::vector<std::vector<double>>& fvec, 
//     double lambda) {

//     IntrinsicCalib calib;
//     calib.pp = pp;

//     ceres::Problem problem;
//     ceres::LossFunction* loss_function_data = new ceres::HuberLoss(1e-4);

//     if (fvec.empty()) {
//         fvec.resize(points2D.size());
//         for (size_t i = 0; i < points2D.size(); ++i) {
//             fvec[i].resize(points2D[i].size(), 0);
//         }
//     }

//     // Add data cost
//     for (size_t cam_ind = 0; cam_ind < points2D.size(); ++cam_ind) {
//         for (size_t pt_ind = 0; pt_ind < points2D[cam_ind].size(); ++pt_ind) {
//             Eigen::Vector2d z = points2D[cam_ind][pt_ind] - pp;
//             Eigen::Vector3d Z = poses[cam_ind].apply(points3D[cam_ind][pt_ind]);

//             double& f = fvec[cam_ind][pt_ind];
//             if (f == 0) { // Initialize if not yet initialized
//                 f = (z.squaredNorm() * Z[2]) / (Z.topRows<2>().dot(z));
//             }

//             ceres::CostFunction* data_term = FocalDataCost::CreateCost(f);
//             problem.AddResidualBlock(data_term, loss_function_data, &(fvec[cam_ind][pt_ind]));
//         }
//     }

//     // Add regularization with handling parameter uniqueness
//     ceres::LossFunction* scaled_loss = new ceres::ScaledLoss(new ceres::TrivialLoss(), lambda, ceres::TAKE_OWNERSHIP);

//     for (size_t i = 0; i < cost_matrix.pt_index.size(); ++i) {
//         std::vector<double*> params_for_cost;
//         for (size_t j = 0; j < cost_matrix.pt_index[i].size(); ++j) {
//             int cam_idx = cost_matrix.cam_index[i][j];
//             int pt_idx = cost_matrix.pt_index[i][j];
//             params_for_cost.push_back(&fvec[cam_idx][pt_idx]);
//         }

//         ceres::CostFunction* reg_cost = FocalCostMatrixRowCost::CreateCost(
//             cost_matrix.pt_index[i], cost_matrix.cam_index[i], cost_matrix.values[i], fvec, params_for_cost);

//         problem.AddResidualBlock(reg_cost, scaled_loss, params_for_cost);
//     }

//     // Solve the problem
//     ceres::Solver::Options options;
//     options.max_num_iterations = 100;
//     options.linear_solver_type = ceres::SPARSE_NORMAL_CHOLESKY;
//     options.minimizer_progress_to_stdout = true;
//     ceres::Solver::Summary summary;
//     ceres::Solve(options, &problem, &summary);

//     // Output the result
//     std::cout << summary.FullReport() << "\n";

//     return calib;
// }

IntrinsicCalib calibrate_fix_lambda_multi(const std::vector<std::vector<Eigen::Vector2d>> &points2D, 
                                const std::vector<std::vector<Eigen::Vector3d>> &points3D,
                                const CostMatrix &cost_matrix, const Eigen::Vector2d &pp,
                                const std::vector<CameraPose> &poses, double lambda) {
    
    std::vector<std::vector<double>> fvec;
    return calibrate_fix_lambda_multi_w_initial_guess(points2D, points3D, cost_matrix, pp, poses, fvec, lambda);
}


IntrinsicCalib calibrate(const std::vector<Eigen::Vector2d> &points2D, 
                                const std::vector<Eigen::Vector3d> &points3D,
                                const CostMatrix &cost_matrix, const Eigen::Vector2d &pp,
                                const CameraPose &pose,
                                const double max_error) {
    return calibrate_multi({points2D}, {points3D}, cost_matrix, pp, {pose}, max_error);
}

double compute_tangential_error(const std::vector<std::vector<Eigen::Vector2d>> &points2D, 
                                const std::vector<std::vector<Eigen::Vector3d>> &points3D_cam,
                                const std::vector<CameraPose> &poses,
                                const Eigen::Vector2d &pp,
                                const std::vector<std::vector<double>> &fvec,
                                const double max_sq_error) {
    double rms_tan = 0.0;
    size_t num_pts = 0;
    for (size_t cam_k = 0; cam_k < points2D.size(); ++cam_k) {
        std::vector<Eigen::Vector2d> proj(points2D[cam_k].size());

        for (int i = 0; i < points2D[cam_k].size(); ++i){
            proj[i] = fvec[cam_k][i] * points3D_cam[cam_k][i].topRows<2>() / points3D_cam[cam_k][i][2] + pp;
        }
        
        for (size_t pt_k = 0; pt_k < points2D[cam_k].size(); ++pt_k) {
            Eigen::Vector2d r = proj[pt_k] - points2D[cam_k][pt_k];
            Eigen::Vector2d z = proj[pt_k] - pp;
            z.normalize();
            double alpha = z.dot(r);
            rms_tan += std::min(max_sq_error, alpha*alpha);
            num_pts++;
        }
    }
    rms_tan = std::sqrt(rms_tan / num_pts);

    return rms_tan;
}

IntrinsicCalib calibrate_multi(const std::vector<std::vector<Eigen::Vector2d>> &points2D, 
                                const std::vector<std::vector<Eigen::Vector3d>> &points3D,
                                const CostMatrix &cost_matrix, const Eigen::Vector2d &pp,
                                const std::vector<CameraPose> &poses,
                                const double max_error) {
    // We start by computing the radial RMS error
    const double threshold = max_error * max_error;
    double rms_rad = 0.0;
    size_t num_pts = 0;
    for (size_t cam_k = 0; cam_k < points2D.size(); ++cam_k) {
        for (size_t pt_k = 0; pt_k < points2D[cam_k].size(); ++pt_k) {
            Eigen::Vector2d z = poses[cam_k].R().topRows<2>() * points3D[cam_k][pt_k] + poses[cam_k].t.topRows<2>();
            z.normalize();
            Eigen::Vector2d xc = points2D[cam_k][pt_k] - pp;
            rms_rad += std::min(threshold, (xc - z.dot(xc) * z).squaredNorm());
            num_pts++;
        }
    }
    rms_rad = std::sqrt(rms_rad / num_pts);
    std::cout << "Initial RMS = " << rms_rad << "\n";

    const double tol_rms_diff = 0.1;
    const double min_mu = 1e-6;
    const double max_mu = 1e5;
    const size_t max_iters = 20;
    
    double mu = 1e3;
    bool done = false;
    
    // Compute 3D points in the camera coordinate system
    std::vector<std::vector<Eigen::Vector3d>> points3D_cam(points3D.size());
    for (size_t cam_k = 0; cam_k < points2D.size(); ++cam_k) {
        points3D_cam[cam_k].resize(points2D[cam_k].size());
        for (size_t pt_k = 0; pt_k < points2D[cam_k].size(); ++pt_k) {
            points3D_cam[cam_k][pt_k] = poses[cam_k].apply(points3D[cam_k][pt_k]);
        }
    }

    IntrinsicCalib best_calib;
    double best_mu = 1.0;
    double best_score = std::numeric_limits<double>::max();
    double best_rms_tan = std::numeric_limits<double>::max();
    std::vector<std::vector<double>> fvec; // we keep the solution to warm-start next iteration

    std::vector<double> initial_mu = {1e-3, 1e-4, 1e-5};

    for (size_t iter = 0; iter < max_iters; ++iter) {

        if (iter < initial_mu.size()) {
            mu = initial_mu[iter];   
        } else if (iter == initial_mu.size()) {
            mu = best_mu;
            if (best_rms_tan < rms_rad) {
                mu *= 5.0;
            } else {
                mu /= 2.0;
            }
        }

        fvec.clear();
        IntrinsicCalib calib = calibrate_fix_lambda_multi_w_initial_guess(points2D, points3D, cost_matrix, pp, poses, fvec, mu);

        // Compute tangential error
        double rms_tan = compute_tangential_error(points2D, points3D_cam, poses, calib.pp, fvec, threshold);

        double res = std::abs(rms_rad - rms_tan);
        std::cout << "iter=" << iter << ", mu = " << mu << ", rms_rad=" << rms_rad << ", rms_tan=" << rms_tan << ", res=" << res <<  "\n";

        if (res < best_score) {
            best_calib = calib;
            best_score = res;
            best_mu = mu;
            best_rms_tan = rms_tan;
        }


        if (rms_tan < rms_rad) {
            mu *= 5.0;
            if (mu > max_mu) {
                std::cout << "mu > " << max_mu << "\n";
                break;
            }
        } else {
            mu /= 2.0;
            if (mu < min_mu) {
                std::cout << "mu < " << min_mu << "\n";
                break;
            }
        }
        
        if (res < tol_rms_diff) {
            std::cout << "res < " << tol_rms_diff << "\n";
            break;
        }
    }
    return best_calib;
}

std::vector<Eigen::Vector3d> undistort(const std::vector<Eigen::Vector2d> &points2D, const IntrinsicCalib& calib) {
    std::vector<Eigen::Vector3d> calib_pts(points2D.size());

    for (size_t pt_k = 0; pt_k < points2D.size(); ++pt_k) {
        Eigen::Vector2d x = points2D[pt_k] - calib.pp;
        double r = x.norm();

        size_t k1 = 0, k2 = 1;

        if (r < calib.r_f[0].first) {
            k1 = 0;
            k2 = 1;
        } else if (r > calib.r_f[calib.r_f.size()-1].first) {
            k1 = calib.r_f.size()-2;
            k2 = calib.r_f.size()-1;
        } else {            
            for (size_t k = 0; k < calib.r_f.size() - 1; ++k) {
                if (calib.r_f[k].first < r && r <= calib.r_f[k+1].first) {
                    k1 = k;
                    k2 = k+1;
                    break;
                }
            }
        }
        
        const double r1 = calib.r_f[k1].first;
        const double r2 = calib.r_f[k2].first;
        const double f1 = calib.r_f[k1].second;
        const double f2 = calib.r_f[k2].second;
        
        const double k = (f2 - f1) / (r2 - r1);
        const double m = f1 - k * r1;
        const double f = k * r + m;

        calib_pts[pt_k] << x(0), x(1), f;
        calib_pts[pt_k].normalize();
    }

    return calib_pts;
}

std::vector<Eigen::Vector2d> distort(const std::vector<Eigen::Vector3d> &points3D, const IntrinsicCalib& calib) {
    std::vector<Eigen::Vector2d> proj_pts(points3D.size());

    const std::vector<std::pair<double,double>> &theta_r = calib.theta_r;

    for (size_t k = 0; k < points3D.size(); ++k) {
        proj_pts[k] = points3D[k].topRows<2>();
        double rho = proj_pts[k].norm();
        double z = points3D[k](2);
        double theta = std::atan2(rho,z);

        if (theta <= theta_r[0].first) {
            // special case, extrpolate from first points
            double dtheta = theta_r[1].first - theta_r[0].first;
            double alpha = (theta - theta_r[0].first) / dtheta;
            double r = (1.0 - alpha) * theta_r[0].second + alpha * theta_r[1].second;
            proj_pts[k] *= r / rho;

        } else if (theta >= theta_r[theta_r.size()-1].first) {
            // special case, extrapolate from last points
            double dtheta = theta_r[theta_r.size()-1].first - theta_r[theta_r.size()-2].first;
            double alpha = (theta - theta_r[theta_r.size()-2].first) / dtheta;
            double r = (1.0 - alpha) * theta_r[theta_r.size()-2].second + alpha * theta_r[theta_r.size()-1].second;
            proj_pts[k] *= r / rho;

        } else {
            // we interpolate between the two neighboring calibration points
            auto it = std::lower_bound(theta_r.begin(), theta_r.end(), std::make_pair(theta, std::numeric_limits<double>::lowest()));
            
            const std::pair<double,double> &theta_r1 = *it;
            const std::pair<double,double> &theta_r0 = *(--it);
            
            // Interpolate radius           
            double dtheta = theta_r1.first - theta_r0.first;
            double alpha = (theta - theta_r0.first) / dtheta;
            double r = (1.0 - alpha) * theta_r0.second + alpha * theta_r1.second;

            proj_pts[k] *= r / rho;
        }
        proj_pts[k] += calib.pp;
    }

    return proj_pts;
}
}  // namespace colmap