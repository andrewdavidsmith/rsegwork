/*
 * Copyright (C) 2011 University of Southern California
 *                    Andrew D Smith and Qiang Song
 * Author: Qiang Song and Andrew D. Smith
 *
 * This is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include "ThreeStateScaleSplitResolveMixture.hpp"
#include "numerical_utils.hpp"

#include <memory>
#include <cmath>
#include <iomanip>
#include <limits>

#include "RNG.hpp"

using std::vector;
using std::auto_ptr;
using std::cout;
using std::cerr;
using std::endl;
using std::pair;
using std::make_pair;

static void
initialize_values(const vector<double> &values, 
                  const vector<double> &values_a, 
                  const vector<double> &values_b, 
                  const vector<double> &scales,
                  SplitDistro &fg_distro,
                  SplitDistro &mid_distro,
                  SplitDistro &bg_distro) {
		 
    vector<double> fg_part_a;
    vector<double> fg_part_b;
    vector<double> fg_scales;
    
    vector<double> mid_part_a;
    vector<double> mid_part_b;
    vector<double> mid_scales;
	
    vector<double> bg_part_a;
    vector<double> bg_part_b;
    vector<double> bg_scales;

    double val_weight = 0;
    for (size_t i = 0; i < values.size(); ++i)
        val_weight += fabs(values[i]);
  
    assert(val_weight > 0);
    double val_per_part = std::ceil(val_weight/3);
  
    vector<pair<double, size_t> > helper;
    for (size_t i = 0; i < values.size(); ++i)
        helper.push_back(make_pair(values[i], i));
    sort(helper.begin(), helper.end(), std::greater<pair<double, size_t> >());
  
    size_t i = 0;
    //   const size_t lim1 = helper.size()/3;

    double sum = 0;
    for (; i < helper.size() && sum < val_per_part; ++i) {
        // for (; i < lim1; ++i) {
        fg_part_a.push_back(values_a[helper[i].second]);
        fg_part_b.push_back(values_b[helper[i].second]);
        fg_scales.push_back(scales[helper[i].second]);
        sum += fabs(helper[i].first);
    }
    //   const size_t lim2 = 2*helper.size()/3;
    sum = 0;
    for (; i < helper.size() && sum < val_per_part; ++i) {
        // for (; i < lim2; ++i) {
        mid_part_a.push_back(values_a[helper[i].second]);
        mid_part_b.push_back(values_b[helper[i].second]);
        mid_scales.push_back(scales[helper[i].second]);
        sum += fabs(helper[i].first);
    }
    for (; i < helper.size(); ++i) {
        bg_part_a.push_back(values_a[helper[i].second]);
        bg_part_b.push_back(values_b[helper[i].second]);
        bg_scales.push_back(scales[helper[i].second]);
    }

    // Obtain initial estimates of distribution values
    fg_distro.estimate_params_ml(fg_part_a, fg_part_b, fg_scales);
    mid_distro.estimate_params_ml(mid_part_a, mid_part_b, mid_scales); 
    bg_distro.estimate_params_ml(bg_part_a, bg_part_b, bg_scales);
}

static double
expectation_step(const vector<double> &values, 
                 const vector<double> &scales,
                 const vector<double> &mixing,
                 const SplitDistro &fg_distro, 
                 const SplitDistro &mid_distro, 
                 const SplitDistro &bg_distro,
                 vector<double> &fg_probs, 
                 vector<double> &mid_probs, 
                 vector<double> &bg_probs) {
  
    double score = 0;
  
    const double fg_log_mixing = log(mixing[0]);
    assert(finite(fg_log_mixing));
    const double mid_log_mixing = log(mixing[1]);
    assert(finite(mid_log_mixing));
    const double bg_log_mixing = log(mixing[2]);
    assert(finite(bg_log_mixing));
  
    for (size_t i = 0; i < values.size(); ++i) {
    
        const double fg_part =
            fg_log_mixing + fg_distro.log_likelihood(values[i], scales[i]);
        assert(finite(fg_part));
    
        const double mid_part =
            mid_log_mixing + mid_distro.log_likelihood(values[i], scales[i]);
        if (!finite(mid_part)) {
            cout << values[i] << "\t" << mid_distro.log_likelihood(values[i]) << endl;
            cout << mid_distro << endl;
        }
        assert(finite(mid_part));
        const double bg_part =
            bg_log_mixing + bg_distro.log_likelihood(values[i], scales[i]);
        assert(finite(fg_part));
    
        const double denom = log_sum_log(fg_part, mid_part, bg_part);
        assert(finite(denom));
    
        fg_probs[i] = exp(fg_part - denom);
        mid_probs[i] = exp(mid_part - denom);
        bg_probs[i] = exp(bg_part - denom);
    
        score += denom;
    }
  
    return score;
}


static void
maximization_step(const vector<double> &values, 
                  const vector<double> &vals_a, 
                  const vector<double> &vals_b, 
                  const vector<double> &scales,
                  const vector<double> &fg_probs,
                  const vector<double> &mid_probs,
                  const vector<double> &bg_probs,
                  vector<double> &mixing,
                  SplitDistro &fg_distro, 
                  SplitDistro &mid_distro, 
                  SplitDistro &bg_distro) {

    //   cerr << "FG Distro:" << endl;
    fg_distro.estimate_params_ml(vals_a, vals_b, scales, fg_probs);
    //   cerr << "MID Distro:" << endl;
    mid_distro.estimate_params_ml(vals_a, vals_b, scales, mid_probs);
    //   cerr << "BG Distro:" << endl;
    bg_distro.estimate_params_ml(vals_a, vals_b, scales, bg_probs);

    vector<double> log_fg_probs(fg_probs);
    vector<double> log_mid_probs(mid_probs);
    vector<double> log_bg_probs(bg_probs);
    for (size_t i = 0; i < log_fg_probs.size(); ++i) {
        log_fg_probs[i] = log(log_fg_probs[i]);
        log_mid_probs[i] = log(log_mid_probs[i]);
        log_bg_probs[i] = log(log_bg_probs[i]);
    }    
  
    mixing[0] = SplitDistro::log_sum_log_vec(log_fg_probs, log_fg_probs.size());
    mixing[1] = SplitDistro::log_sum_log_vec(log_mid_probs, log_mid_probs.size());
    mixing[2] = SplitDistro::log_sum_log_vec(log_bg_probs, log_bg_probs.size());
    const double mix_sum = log_sum_log(mixing[0], mixing[1], mixing[2]);
  
    mixing[0] = exp(mixing[0] - mix_sum);
    mixing[1] = exp(mixing[1] - mix_sum);
    mixing[2] = exp(mixing[2] - mix_sum);
}

void
ThreeStateScaleSplitResolveMixture(const vector<double> &values,
                              const vector<double> &vals_a,
                              const vector<double> &vals_b,
                              const std::vector<double> &scales,
                              const size_t max_iterations, 
                              const double tolerance, 
                              const int VERBOSE,
                              SplitDistro &fg_distro,
                              SplitDistro &mid_distro,
                              SplitDistro &bg_distro, 
                              std::vector<double> &mixing) 
{
    static const size_t THREE = 3;
  
    // partition the observations to get the initial guess
    initialize_values(values, vals_a, vals_b, scales,
                     fg_distro, mid_distro, bg_distro);
    
    
    vector<double> fg_probs(values.size(), 0);
    vector<double> mid_probs(values.size(), 0);
    vector<double> bg_probs(values.size(), 0);
    
    mixing = vector<double>(THREE, 1.0/THREE);
    
    if (VERBOSE)
        cout << endl << std::setw(10) << "DELTA"
             << std::setw(14) << "(PARAMS,MIX)" << endl;
    
    // Do the expectation maximization
    double prev_score = std::numeric_limits<double>::max();
    for (size_t itr = 0; itr < max_iterations; ++itr) {
        const double score = 
            expectation_step(values, scales,
                             mixing, fg_distro, mid_distro, bg_distro, 
                             fg_probs, mid_probs, bg_probs);
        maximization_step(values, vals_a, vals_b, scales,
                          fg_probs, mid_probs, bg_probs, 
                          mixing, fg_distro, mid_distro, bg_distro);
    
        if (VERBOSE) {
            cout << std::setw(10) << std::setprecision(4) 
                 << (prev_score - score)/prev_score << endl
                 << std::setw(14) << fg_distro << " " 
                 << std::setw(10) << mixing[0] << endl
                 << std::setw(14) << mid_distro << " " 
                 << std::setw(10) << mixing[1] << endl
                 << std::setw(14) << bg_distro << " " 
                 << std::setw(10) << mixing[2] << endl
                 << endl;
        }
        if (std::fabs((prev_score - score)/prev_score) < tolerance)
            break;
        prev_score = score;
    }
}
