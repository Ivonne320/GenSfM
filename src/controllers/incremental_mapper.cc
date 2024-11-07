// Copyright (c) 2018, ETH Zurich and UNC Chapel Hill.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//
//     * Neither the name of ETH Zurich and UNC Chapel Hill nor the names of
//       its contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Author: Johannes L. Schoenberger (jsch-at-demuc-dot-de)

#include "controllers/incremental_mapper.h"
#include "estimators/implicit_bundle_adjustment.h"

#include "util/misc.h"

namespace colmap {
namespace {

size_t TriangulateImage(const IncrementalMapperOptions& options,
                        const Image& image, IncrementalMapper* mapper) {
  std::cout << "  => Continued observations: " << image.NumPoints3D()
            << std::endl;
  // const size_t num_reg_images = mapper->GetReconstruction().NumRegImages();
  // bool standard_triangulation = num_reg_images >= options.Triangulation().min_num_reg_images;
  const size_t num_tris =
      mapper->TriangulateImage(options.Triangulation(), image.ImageId());
  std::cout << "  => Added observations: " << num_tris << std::endl;
  return num_tris;
}

void AdjustGlobalBundle(const IncrementalMapperOptions& options,
                        IncrementalMapper* mapper, bool initial = false) {
  BundleAdjustmentOptions custom_ba_options = options.GlobalBundleAdjustment();
  

  const size_t num_reg_images = mapper->GetReconstruction().NumRegImages();
  // min_num_reg_images related
  custom_ba_options.refine_extra_params = (num_reg_images >= 16); 
  // options.Mapper().ba_refine_extra_params = (num_reg_images > 20);

  // Use stricter convergence criteria for first registered images.
  const size_t kMinNumRegImagesForFastBA = 10;
  if (num_reg_images < kMinNumRegImagesForFastBA) {
    custom_ba_options.solver_options.function_tolerance /= 10;
    custom_ba_options.solver_options.gradient_tolerance /= 10;
    custom_ba_options.solver_options.parameter_tolerance /= 10;
    custom_ba_options.solver_options.max_num_iterations *= 2;
    custom_ba_options.solver_options.max_linear_solver_iterations = 200;
  }

  PrintHeading1("Global bundle adjustment");
  if (options.ba_global_use_pba && !options.fix_existing_images &&
      num_reg_images >= kMinNumRegImagesForFastBA &&
      ParallelBundleAdjuster::IsSupported(custom_ba_options,
                                          mapper->GetReconstruction())) {
    mapper->AdjustParallelGlobalBundle(
        custom_ba_options, options.ParallelGlobalBundleAdjustment(),initial);
  } else {
    mapper->AdjustGlobalBundle(options.Mapper(), custom_ba_options, initial);
  }
}

void IterativeLocalRefinement(const IncrementalMapperOptions& options,
                              const image_t image_id,
                              IncrementalMapper* mapper) {
  std::cout << "  =>Entered Colmap Local bundle adjustment" << std::endl;
  auto ba_options = options.LocalBundleAdjustment();
  size_t num_reg_images = mapper->GetReconstruction().NumRegImages();
  // min_num_reg_images related
  // ba_options.refine_extra_params = (num_reg_images > 16); 
  // options.Mapper().ba_refine_extra_params = (num_reg_images > 20);
  for (int i = 0; i < options.ba_local_max_refinements; ++i) {
    const auto report = mapper->AdjustLocalBundle(
        options.Mapper(), ba_options, options.Triangulation(), image_id,
        mapper->GetModifiedPoints3D());
    std::cout << "  => Merged observations: " << report.num_merged_observations
              << std::endl;
    std::cout << "  => Completed observations: "
              << report.num_completed_observations << std::endl;
    std::cout << "  => Filtered observations: "
              << report.num_filtered_observations << std::endl;
    const double changed =
        (report.num_merged_observations + report.num_completed_observations +
         report.num_filtered_observations) /
        static_cast<double>(report.num_adjusted_observations + 1);
    std::cout << StringPrintf("  => Changed observations: %.6f", changed)
              << std::endl;
    if (changed < options.ba_local_max_refinement_change) {
      break;
    }
    // Only use robust cost function for first iteration.
    ba_options.loss_function_type =
        BundleAdjustmentOptions::LossFunctionType::TRIVIAL;
  }
  mapper->ClearModifiedPoints3D();
}


void IterativeImplicitLocalRefinement(const IncrementalMapperOptions& options,
                              const image_t image_id,
                              IncrementalMapper* mapper) {
  auto ba_options = options.LocalBundleAdjustment();
  ImplicitBundleAdjustmentOptions implicit_ba_options;
  const auto report = mapper->ImplicitAdjustLocalBundle(
        options.Mapper(), ba_options, options.Triangulation(), image_id,
        mapper->GetModifiedPoints3D(), implicit_ba_options);
  // for (int i = 0; i < options.ba_local_max_refinements; ++i) {
    
  //   const auto report = mapper->ImplicitAdjustLocalBundle(
  //       options.Mapper(), ba_options, options.Triangulation(), image_id,
  //       mapper->GetModifiedPoints3D(), implicit_ba_options);
        
  //   std::cout << "  => Merged observations: " << report.num_merged_observations
  //             << std::endl;
  //   std::cout << "  => Completed observations: "
  //             << report.num_completed_observations << std::endl;
  //   std::cout << "  => Filtered observations: "
  //             << report.num_filtered_observations << std::endl;
  //   const double changed =
  //       (report.num_merged_observations + report.num_completed_observations +
  //        report.num_filtered_observations) /
  //       static_cast<double>(report.num_adjusted_observations);
  //   std::cout << StringPrintf("  => Changed observations: %.6f", changed)
  //             << std::endl;
  //   if (changed < options.ba_local_max_refinement_change) {
  //     break;
  //   }
  //   // Only use robust cost function for first iteration.
  //   ba_options.loss_function_type =
  //       BundleAdjustmentOptions::LossFunctionType::TRIVIAL;
  // }
  // mapper->ClearModifiedPoints3D();
}

void ImplicitAdjustGlobalBundle(const IncrementalMapperOptions& options,
                        IncrementalMapper* mapper, bool initial = false) {
  BundleAdjustmentOptions custom_ba_options = options.GlobalBundleAdjustment();
  ImplicitBundleAdjustmentOptions implicit_ba_options;
  const size_t num_reg_images = mapper->GetReconstruction().NumRegImages();
  std::cout << "  =>Entered Implicit Global bundle adjustment" << std::endl;  
  // Use stricter convergence criteria for first registered images.
  const size_t kMinNumRegImagesForFastBA = 10;
  if (num_reg_images < kMinNumRegImagesForFastBA) {
    custom_ba_options.solver_options.function_tolerance /= 10;
    custom_ba_options.solver_options.gradient_tolerance /= 10;
    custom_ba_options.solver_options.parameter_tolerance /= 10;
    custom_ba_options.solver_options.max_num_iterations *= 2;
    custom_ba_options.solver_options.max_linear_solver_iterations = 200;
  }

  PrintHeading1("Global bundle adjustment");
  if (options.ba_global_use_pba && !options.fix_existing_images &&
      num_reg_images >= kMinNumRegImagesForFastBA &&
      ParallelBundleAdjuster::IsSupported(custom_ba_options,
                                          mapper->GetReconstruction())) {
    mapper->AdjustParallelGlobalBundle(
        custom_ba_options, options.ParallelGlobalBundleAdjustment(),initial);
  } else {
    mapper->ImplicitAdjustGlobalBundle(options.Mapper(), custom_ba_options, implicit_ba_options, initial);
  }
}



void ImplicitIterativeGlobalBA(const IncrementalMapperOptions& options,
                        IncrementalMapper* mapper){
  PrintHeading1("Retriangulation");
  CompleteAndMergeTracks(options, mapper);
  std::cout << "  => Retriangulated observations: "
            << mapper->Retriangulate(options.Triangulation()) << std::endl;
  BundleAdjustmentOptions custom_ba_options = options.GlobalBundleAdjustment();
  ImplicitBundleAdjustmentOptions implicit_ba_options;
  const size_t num_reg_images = mapper->GetReconstruction().NumRegImages();

  // Use stricter convergence criteria for first registered images.
  const size_t kMinNumRegImagesForFastBA = 10;
  if (num_reg_images < kMinNumRegImagesForFastBA) {
    custom_ba_options.solver_options.function_tolerance /= 10;
    custom_ba_options.solver_options.gradient_tolerance /= 10;
    custom_ba_options.solver_options.parameter_tolerance /= 10;
    custom_ba_options.solver_options.max_num_iterations *= 2;
    custom_ba_options.solver_options.max_linear_solver_iterations = 200;
  }
  // PrintHeading1("Global bundle adjustment");
  if (options.ba_global_use_pba && !options.fix_existing_images &&
      num_reg_images >= kMinNumRegImagesForFastBA &&
      ParallelBundleAdjuster::IsSupported(custom_ba_options,
                                          mapper->GetReconstruction())) {
    mapper->AdjustParallelGlobalBundle(
        custom_ba_options, options.ParallelGlobalBundleAdjustment());
  } else {
    mapper->ImplicitAdjustGlobalBundle(options.Mapper(), custom_ba_options, implicit_ba_options);
  }

}






void IterativeGlobalRefinement(const IncrementalMapperOptions& options,
                               IncrementalMapper* mapper) {
  std::cout << "  =>Entered Colmap Global bundle adjustment" << std::endl;
  PrintHeading1("Retriangulation");
  CompleteAndMergeTracks(options, mapper);
  std::cout << "  => Retriangulated observations: "
            << mapper->Retriangulate(options.Triangulation()) << std::endl;
  // FilterImages(options, mapper);

  for (int i = 0; i < options.ba_global_max_refinements; ++i) {
    const size_t num_observations =
        mapper->GetReconstruction().ComputeNumObservations();
    size_t num_changed_observations = 0;
    AdjustGlobalBundle(options, mapper);
    num_changed_observations += CompleteAndMergeTracks(options, mapper);
    num_changed_observations += FilterPoints(options, mapper);
    const double changed =
        static_cast<double>(num_changed_observations) / num_observations;
    std::cout << StringPrintf("  => Changed observations: %.6f", changed)
              << std::endl;
    if (changed < options.ba_global_max_refinement_change) {
      break;
    }
  }

  FilterImages(options, mapper);
}

void ExtractColors(const std::string& image_path, const image_t image_id,
                   Reconstruction* reconstruction) {
  if (!reconstruction->ExtractColorsForImage(image_id, image_path)) {
    std::cout << StringPrintf("WARNING: Could not read image %s at path %s.",
                              reconstruction->Image(image_id).Name().c_str(),
                              image_path.c_str())
              << std::endl;
  }
}

void WriteSnapshot(const Reconstruction& reconstruction,
                   const std::string& snapshot_path) {
  PrintHeading1("Creating snapshot");
  // Get the current timestamp in milliseconds.
  const size_t timestamp =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::high_resolution_clock::now().time_since_epoch())
          .count();
  // Write reconstruction to unique path with current timestamp.
  const std::string path =
      JoinPaths(snapshot_path, StringPrintf("%010d", timestamp));
  CreateDirIfNotExists(path);
  std::cout << "  => Writing to " << path << std::endl;
  reconstruction.Write(path);
}

}  // namespace

size_t FilterPoints(const IncrementalMapperOptions& options,
                    IncrementalMapper* mapper) {
  const size_t num_filtered_observations =
      mapper->FilterPoints(options.Mapper());
  std::cout << "  => Filtered observations: " << num_filtered_observations
            << std::endl;
  return num_filtered_observations;
}
size_t FilterPointsFinal(const IncrementalMapperOptions& options,
                    IncrementalMapper* mapper) {
  const size_t num_filtered_observations =
      mapper->FilterPointsFinal(options.Mapper());
  std::cout << "  => Filtered observations: " << num_filtered_observations
            << std::endl;
  return num_filtered_observations;
}

size_t FilterImages(const IncrementalMapperOptions& options,
                    IncrementalMapper* mapper) {
  const size_t num_filtered_images = mapper->FilterImages(options.Mapper());
  std::cout << "  => Filtered images: " << num_filtered_images << std::endl;
  return num_filtered_images;
}

size_t CompleteAndMergeTracks(const IncrementalMapperOptions& options,
                              IncrementalMapper* mapper) {
  const size_t num_completed_observations =
      mapper->CompleteTracks(options.Triangulation());
  std::cout << "  => Completed observations: " << num_completed_observations
            << std::endl;
  const size_t num_merged_observations = 
      mapper->MergeTracks(options.Triangulation());
  std::cout << "  => Merged observations: " << num_merged_observations
            << std::endl;
  return num_completed_observations + num_merged_observations;
}

IncrementalMapper::Options IncrementalMapperOptions::Mapper() const {
  IncrementalMapper::Options options = mapper;
  options.abs_pose_refine_focal_length = ba_refine_focal_length;
  options.abs_pose_refine_extra_params = ba_refine_extra_params;
  options.min_focal_length_ratio = min_focal_length_ratio;
  options.max_focal_length_ratio = max_focal_length_ratio;
  options.max_extra_param = max_extra_param;
  options.num_threads = num_threads;
  options.local_ba_num_images = ba_local_num_images;
  options.fix_existing_images = fix_existing_images;
  return options;
}

IncrementalTriangulator::Options IncrementalMapperOptions::Triangulation()
    const {
  IncrementalTriangulator::Options options = triangulation;
  options.min_focal_length_ratio = min_focal_length_ratio;
  options.max_focal_length_ratio = max_focal_length_ratio;
  options.max_extra_param = max_extra_param;
  // related to min_num_reg_images
  options.min_num_reg_images = 16;
  return options;
}

BundleAdjustmentOptions IncrementalMapperOptions::LocalBundleAdjustment()
    const {
  BundleAdjustmentOptions options;
  options.solver_options.function_tolerance = 0.0;
  options.solver_options.gradient_tolerance = 10.0;
  options.solver_options.parameter_tolerance = 0.0;
  options.solver_options.max_num_iterations = ba_local_max_num_iterations;
  options.solver_options.max_linear_solver_iterations = 100;
  options.solver_options.minimizer_progress_to_stdout = false;
  options.solver_options.num_threads = num_threads;
#if CERES_VERSION_MAJOR < 2
  options.solver_options.num_linear_solver_threads = num_threads;
#endif  // CERES_VERSION_MAJOR
  options.print_summary = true;
  options.refine_focal_length = ba_refine_focal_length;
  options.refine_principal_point = ba_refine_principal_point;
  options.refine_extra_params = ba_refine_extra_params;
  options.min_num_residuals_for_multi_threading =
      ba_min_num_residuals_for_multi_threading;
  options.loss_function_scale = 1.0;
  options.loss_function_type =
      BundleAdjustmentOptions::LossFunctionType::SOFT_L1;
      
  options.min_num_reg_images = Triangulation().min_num_reg_images;
  return options;
}

BundleAdjustmentOptions IncrementalMapperOptions::GlobalBundleAdjustment()
    const {
  BundleAdjustmentOptions options;
  options.solver_options.function_tolerance = 0.0;
  options.solver_options.gradient_tolerance = 1.0;
  options.solver_options.parameter_tolerance = 0.0;
  options.solver_options.max_num_iterations = ba_global_max_num_iterations;
  options.solver_options.max_linear_solver_iterations = 100;
  options.solver_options.minimizer_progress_to_stdout = false;
  options.solver_options.num_threads = num_threads;
#if CERES_VERSION_MAJOR < 2
  options.solver_options.num_linear_solver_threads = num_threads;
#endif  // CERES_VERSION_MAJOR
  options.print_summary = true;
  options.refine_focal_length = ba_refine_focal_length;
  options.refine_principal_point = ba_refine_principal_point;
  options.refine_extra_params = ba_refine_extra_params;
  options.min_num_residuals_for_multi_threading =
	  ba_min_num_residuals_for_multi_threading;
  options.loss_function_type =
      BundleAdjustmentOptions::LossFunctionType::TRIVIAL;
  
  options.min_num_reg_images = Triangulation().min_num_reg_images;
  return options;
}

ParallelBundleAdjuster::Options
IncrementalMapperOptions::ParallelGlobalBundleAdjustment() const {
  ParallelBundleAdjuster::Options options;
  options.max_num_iterations = ba_global_max_num_iterations;
  options.print_summary = true;
  options.gpu_index = ba_global_pba_gpu_index;
  options.num_threads = num_threads;
  options.min_num_residuals_for_multi_threading =
	  ba_min_num_residuals_for_multi_threading;
  return options;
}

bool IncrementalMapperOptions::Check() const {
  CHECK_OPTION_GT(min_num_matches, 0);
  CHECK_OPTION_GT(max_num_models, 0);
  CHECK_OPTION_GT(max_model_overlap, 0);
  CHECK_OPTION_GE(min_model_size, 0);
  CHECK_OPTION_GT(init_num_trials, 0);
  CHECK_OPTION_GT(min_focal_length_ratio, 0);
  CHECK_OPTION_GT(max_focal_length_ratio, 0);
  CHECK_OPTION_GE(max_extra_param, 0);
  CHECK_OPTION_GE(ba_local_num_images, 2);
  CHECK_OPTION_GE(ba_local_max_num_iterations, 0);
  CHECK_OPTION_GT(ba_global_images_ratio, 1.0);
  CHECK_OPTION_GT(ba_global_points_ratio, 1.0);
  CHECK_OPTION_GT(ba_global_images_freq, 0);
  CHECK_OPTION_GT(ba_global_points_freq, 0);
  CHECK_OPTION_GT(ba_global_max_num_iterations, 0);
  CHECK_OPTION_GT(ba_local_max_refinements, 0);
  CHECK_OPTION_GE(ba_local_max_refinement_change, 0);
  CHECK_OPTION_GT(ba_global_max_refinements, 0);
  CHECK_OPTION_GE(ba_global_max_refinement_change, 0);
  CHECK_OPTION_GE(snapshot_images_freq, 0);
  CHECK_OPTION(Mapper().Check());
  CHECK_OPTION(Triangulation().Check());
  return true;
}

IncrementalMapperController::IncrementalMapperController(
    const IncrementalMapperOptions* options, const std::string& image_path,
    const std::string& database_path,
    ReconstructionManager* reconstruction_manager)
    : options_(options),
      image_path_(image_path),
      database_path_(database_path),
      reconstruction_manager_(reconstruction_manager) {
  CHECK(options_->Check());
  RegisterCallback(INITIAL_IMAGE_PAIR_REG_CALLBACK);
  RegisterCallback(NEXT_IMAGE_REG_CALLBACK);
  RegisterCallback(LAST_IMAGE_REG_CALLBACK);
}

void IncrementalMapperController::Run() {
  if (!LoadDatabase()) {
    return;
  }

  IncrementalMapper::Options init_mapper_options = options_->Mapper();
  Reconstruct(init_mapper_options);

  const size_t kNumInitRelaxations = 2;
  for (size_t i = 0; i < kNumInitRelaxations; ++i) {
    if (reconstruction_manager_->Size() > 0 || IsStopped()) {
      break;
    }

    std::cout << "  => Relaxing the initialization constraints." << std::endl;
    init_mapper_options.init_min_num_inliers /= 2;
    Reconstruct(init_mapper_options);

    if (reconstruction_manager_->Size() > 0 || IsStopped()) {
      break;
    }

    std::cout << "  => Relaxing the initialization constraints." << std::endl;
    init_mapper_options.init_min_tri_angle /= 2;
    Reconstruct(init_mapper_options);
  }

  std::cout << std::endl;
  GetTimer().PrintMinutes();
}

bool IncrementalMapperController::LoadDatabase() {
  PrintHeading1("Loading database");

  // Make sure images of the given reconstruction are also included when
  // manually specifying images for the reconstrunstruction procedure.
  std::unordered_set<std::string> image_names = options_->image_names;
  if (reconstruction_manager_->Size() == 1 && !options_->image_names.empty()) {
    const Reconstruction& reconstruction = reconstruction_manager_->Get(0);
    for (const image_t image_id : reconstruction.RegImageIds()) {
      const auto& image = reconstruction.Image(image_id);
      image_names.insert(image.Name());
    }
  }

  Database database(database_path_);
  Timer timer;
  timer.Start();
  const size_t min_num_matches = static_cast<size_t>(options_->min_num_matches);
  database_cache_.Load(database, min_num_matches, options_->ignore_watermarks,
                       image_names);
  std::cout << std::endl;
  timer.PrintMinutes();

  std::cout << std::endl;

  if (database_cache_.NumImages() == 0) {
    std::cout << "WARNING: No images with matches found in the database."
              << std::endl
              << std::endl;
    return false;
  }

  return true;
}

void IncrementalMapperController::Reconstruct(
    const IncrementalMapper::Options& init_mapper_options) {
  const bool kDiscardReconstruction = true;

  //////////////////////////////////////////////////////////////////////////////
  // Main loop
  //////////////////////////////////////////////////////////////////////////////

  IncrementalMapper mapper(&database_cache_);

  // Is there a sub-model before we start the reconstruction? I.e. the user
  // has imported an existing reconstruction.
  const bool initial_reconstruction_given = reconstruction_manager_->Size() > 0;
  CHECK_LE(reconstruction_manager_->Size(), 1) << "Can only resume from a "
                                                  "single reconstruction, but "
                                                  "multiple are given.";

  std::vector<image_tuple_t> init_image_tuples; // image_tuple_t = std::tuple<image_t, image_t, image_t, image_t>

  for (int num_trials = 0; num_trials < options_->init_num_trials;
       ++num_trials) {
    BlockIfPaused();
    if (IsStopped()) {
      break;
    }

    size_t reconstruction_idx;
    if (!initial_reconstruction_given || num_trials > 0) {
      reconstruction_idx = reconstruction_manager_->Add();
    } else {
      reconstruction_idx = 0;
    }

    Reconstruction& reconstruction =
        reconstruction_manager_->Get(reconstruction_idx);
    // std::shared_ptr<Reconstruction> reconstruction =
    //     reconstruction_manager_->Get(reconstruction_idx);
    mapper.BeginReconstruction(&reconstruction);

    if(num_trials == 0) {
        mapper.FindInitialImages(init_mapper_options, &init_image_tuples, options_->init_num_trials);
    }

    if(num_trials >= init_image_tuples.size()) {
      break;
    }

    ////////////////////////////////////////////////////////////////////////////
    // Register initial pair
    ////////////////////////////////////////////////////////////////////////////
    if (reconstruction.NumRegImages() == 0) {
      // Try to find good initial pair.
      PrintHeading1("Finding good initial images");
      std::vector<image_t> init_image_ids;
      init_image_ids.push_back(std::get<0>(init_image_tuples[num_trials]));
      init_image_ids.push_back(std::get<1>(init_image_tuples[num_trials]));
      init_image_ids.push_back(std::get<2>(init_image_tuples[num_trials]));
      init_image_ids.push_back(std::get<3>(init_image_tuples[num_trials]));

      PrintHeading1(StringPrintf("Initializing with images #%d, #%d, #%d and #%d.",
                                 init_image_ids[0], init_image_ids[1], init_image_ids[2], init_image_ids[3]));

      const bool reg_init_success = mapper.RegisterInitialImages(
          init_mapper_options, options_->Triangulation(), init_image_ids);
      if (!reg_init_success) {
        std::cout << "  => Initialization failed." << std::endl;
        mapper.EndReconstruction(kDiscardReconstruction);
        reconstruction_manager_->Delete(reconstruction_idx);
        continue;
      }
      // bool initial = true;

      AdjustGlobalBundle(*options_, &mapper);
      FilterPoints(*options_, &mapper);
      FilterImages(*options_, &mapper);

      // Initial image pair failed to register.
      if (reconstruction.NumRegImages() == 0 ||
          reconstruction.NumPoints3D() == 0) {
        mapper.EndReconstruction(kDiscardReconstruction);
        reconstruction_manager_->Delete(reconstruction_idx);
        continue;
      }

      if (options_->extract_colors) {
        ExtractColors(image_path_, init_image_ids[0], &reconstruction);
        ExtractColors(image_path_, init_image_ids[1], &reconstruction);
        ExtractColors(image_path_, init_image_ids[2], &reconstruction);
        ExtractColors(image_path_, init_image_ids[3], &reconstruction);
      }
    }

    Callback(INITIAL_IMAGE_PAIR_REG_CALLBACK);

    ////////////////////////////////////////////////////////////////////////////
    // Incremental mapping
    ////////////////////////////////////////////////////////////////////////////

    size_t snapshot_prev_num_reg_images = reconstruction.NumRegImages();
    size_t ba_prev_num_reg_images = reconstruction.NumRegImages();
    size_t ba_prev_num_points = reconstruction.NumPoints3D();

    bool reg_next_success = true;
    bool prev_reg_next_success = true;

    // If input already has camera intrinsics, perform calibration
    mapper.FilterImages(options_->Mapper());
    mapper.CalibrateCamera(options_->Mapper(), options_->Triangulation());


    while (reg_next_success) {
      BlockIfPaused();
      if (IsStopped()) {
        break;
      }

      reg_next_success = false;

      const std::vector<image_t> next_images =
          mapper.FindNextImages(options_->Mapper());

      if (next_images.empty()) {
        break;
      }

      for (size_t reg_trial = 0; reg_trial < next_images.size(); ++reg_trial) {
        const image_t next_image_id = next_images[reg_trial];
        const Image& next_image = reconstruction.Image(next_image_id);

        PrintHeading1(StringPrintf("Registering image #%d (%d)", next_image_id,
                                   reconstruction.NumRegImages() + 1));

        std::cout << StringPrintf("  => Image sees %d / %d points",
                                  next_image.NumVisiblePoints3D(),
                                  next_image.NumObservations())
                  << std::endl;

        reg_next_success =
            mapper.RegisterNextImage(options_->Mapper(), next_image_id);

        if (reg_next_success) {
          std::cout << "Qvec: " << reconstruction.Image(next_image_id).Qvec().transpose() << std::endl;
          std::cout << "Tvec: " << reconstruction.Image(next_image_id).Tvec().transpose() << std::endl;
          TriangulateImage(*options_, next_image, &mapper);
          // update camera params
          // for (const image_t img_id : reconstruction.RegImageIds()) {
          //       Camera& camera = reconstruction.Camera(reconstruction.Image(img_id).CameraId());
          //       std::cout<<"------- camera.Params(): -------"<< camera.Params()[3]<<std::endl;
          //       camera.UpdateParams();
          //       std::cout<<"------- camera.Params() after updating: -------"<< camera.Params()[3]<<std::endl;
          //     //   // std::cout<<"------- camera.Params(): -------"<< camera.Params()[2]<<std::endl;
          //     }
          for (const auto &[camera_id, camera_const] : reconstruction.Cameras()) {
            Camera& camera = reconstruction.Camera(camera_id);
            std::cout << "Camera " << camera_id << " Params:";
            for (size_t i = 0; i < camera.Params().size(); i++) {
              std::cout << " " << camera.Params()[i];
            }
            std::cout << std::endl;
          }

          // Comment out below for implicit distortion bundle adjustment
          // if (reconstruction.NumRegImages() >= 8){
          // // IterativeLocalRefinement(*options_, next_image_id, &mapper);
          // IterativeImplicitLocalRefinement(*options_, next_image_id, &mapper);
          // ImplicitAdjustGlobalBundle(*options_,&mapper);
          // }
          IterativeLocalRefinement(*options_, next_image_id, &mapper);
          // IterativeImplicitLocalRefinement(*options_, next_image_id, &mapper);
          // IterativeLocalRefinement(*options_, next_image_id, &mapper);
          // TriangulateImage(*options_, next_image, &mapper);
          
          // recalibrate camera per 4 images
          // if(reconstruction.NumRegImages() % 4 == 0){
          //   mapper.CalibrateCamera(options_->Triangulation());
          // }
          if (reconstruction.NumRegImages() >=
                  options_->ba_global_images_ratio * ba_prev_num_reg_images ||
              reconstruction.NumRegImages() >=
                  options_->ba_global_images_freq + ba_prev_num_reg_images ||
              reconstruction.NumPoints3D() >=
                  options_->ba_global_points_ratio * ba_prev_num_points ||
              reconstruction.NumPoints3D() >=
                  options_->ba_global_points_freq + ba_prev_num_points) {
            
            // Only Calibrate camera before global bundle adjustment
            mapper.CalibrateCamera(options_->Mapper(), options_->Triangulation());

            // ImplicitIterativeGlobalBA(*options_, &mapper);
            IterativeGlobalRefinement(*options_, &mapper);
            // ImplicitIterativeGlobalBA(*options_, &mapper);
            // AdjustGlobalBundle(*options_, &mapper);
            // mapper.Retriangulate(options_->Triangulation());
            // ImplicitAdjustGlobalBundle(*options_, &mapper);
            // int min_num_reg_images = *options_->Triangulation().min_num_reg_images;
            // if(reconstruction.NumRegImages() >= 18 && reconstruction.NumRegImages() <= 30){
            //   if(reconstruction.NumRegImages() >= 12 ){
            //   FilterPointsFinal(*options_, &mapper);
            // }
            // FilterPointsFinal(*options_, &mapper);
            // FilterPoints(*options_, &mapper);
            // IterativeGlobalRefinement(*options_, &mapper);
            ba_prev_num_points = reconstruction.NumPoints3D();
            ba_prev_num_reg_images = reconstruction.NumRegImages();
          }


          // //////// preparing for implicit distortion bundle adjustment
          // ImplicitIterativeGlobalBA(*options_, &mapper);
          // ImplicitAdjustGlobalBundle(*options_, &mapper);

          if (options_->extract_colors) {
            ExtractColors(image_path_, next_image_id, &reconstruction);
          }

          if (options_->snapshot_images_freq > 0 &&
              reconstruction.NumRegImages() >=
                  options_->snapshot_images_freq +
                      snapshot_prev_num_reg_images) {
            snapshot_prev_num_reg_images = reconstruction.NumRegImages();
            WriteSnapshot(reconstruction, options_->snapshot_path);
          }

          Callback(NEXT_IMAGE_REG_CALLBACK);

          break;
        } else {
          // for (const image_t img_id : reconstruction.RegImageIds()) {
          //       Camera& camera = reconstruction.Camera(reconstruction.Image(img_id).CameraId());
          //       camera.UpdateParams();
          //     //   // std::cout<<"------- camera.Params(): -------"<< camera.Params()[2]<<std::endl;
          //     }
          std::cout << "  => Could not register, trying another image."
                    << std::endl;

          // If initial pair fails to continue for some time,
          // abort and try different initial pair.
          const size_t kMinNumInitialRegTrials = 30;
          if (reg_trial >= kMinNumInitialRegTrials &&
              reconstruction.NumRegImages() <
                  static_cast<size_t>(options_->min_model_size)) {
            break;
          }
        }
      }

      const size_t max_model_overlap =
          static_cast<size_t>(options_->max_model_overlap);
      if (mapper.NumSharedRegImages() >= max_model_overlap) {
        break;
      }

      // ======================================================================
      // If no image could be registered, try a single final global iterative
      // bundle adjustment and try again to register one image. If this fails
      // once, then exit the incremental mapping.
      if (!reg_next_success && prev_reg_next_success) {
        reg_next_success = true;
        prev_reg_next_success = false;
        // ImplicitAdjustGlobalBundle(*options_, &mapper);
        IterativeGlobalRefinement(*options_, &mapper);
        // ImplicitIterativeGlobalBA(*options_, &mapper);
        // ImplicitAdjustGlobalBundle(*options_, &mapper);
        // FilterPointsFinal(*options_, &mapper);
      } else {
        prev_reg_next_success = reg_next_success;
      }
    }
    // ======================================================================

    if (IsStopped()) {
      const bool kDiscardReconstruction = false;
      mapper.EndReconstruction(kDiscardReconstruction);
      break;
    }
// ======================================================================
    // Only run final global BA, if last incremental BA was not global.
    if (reconstruction.NumRegImages() >= 2 &&
        reconstruction.NumRegImages() != ba_prev_num_reg_images &&
        reconstruction.NumPoints3D() != ba_prev_num_points) {
      IterativeGlobalRefinement(*options_, &mapper);
      // ImplicitIterativeGlobalBA(*options_, &mapper);
      // ImplicitAdjustGlobalBundle(*options_, &mapper);
      // FilterPointsFinal(*options_, &mapper);
    }
    // ImplicitAdjustGlobalBundle(*options_, &mapper);

    // If the total number of images is small then do not enforce the minimum
    // model size so that we can reconstruct small image collections.
    const size_t min_model_size =
        std::min(database_cache_.NumImages(),
                 static_cast<size_t>(options_->min_model_size));
    if ((options_->multiple_models  &&
         reconstruction.NumRegImages() < min_model_size) ||
        reconstruction.NumRegImages() == 0) {
      mapper.EndReconstruction(kDiscardReconstruction);
      // mapper.EndReconstruction(true);
      reconstruction_manager_->Delete(reconstruction_idx);
    } else {
      const bool kDiscardReconstruction = false;
      mapper.EndReconstruction(kDiscardReconstruction);
      //  mapper.EndReconstruction(false);
    }

    Callback(LAST_IMAGE_REG_CALLBACK);

    const size_t max_num_models = static_cast<size_t>(options_->max_num_models);
    if (initial_reconstruction_given || !options_->multiple_models ||
        reconstruction_manager_->Size() >= max_num_models ||
        mapper.NumTotalRegImages() >= database_cache_.NumImages() - 1) {
      break;
    }
  }
}

}  // namespace colmap
