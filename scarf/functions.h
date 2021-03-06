/*
 * SHT functions declarations
 *
 *    Created 2021 by Samuel Simko.
 *    Based on the DUCC source code by Martin Reinecke.
 *
 * This file is subject to the terms and conditions of the GNU General Public License.
 */

#include "phase.h"

using namespace ducc0;
using namespace std;
using namespace ducc0::detail_sharp;

namespace py = pybind11;

/// aliases for pybind11 numpy arrays
using a_d = py::array_t<double>;
using a_s = py::array_t<size_t>;
using a_li = py::array_t<long int>;
using a_d_c = py::array_t<double, py::array::c_style | py::array::forcecast>;
using a_c_c =
    py::array_t<complex<double>, py::array::c_style | py::array::forcecast>;

/// Creates a sharp_alm_info given lmax and mmax
unique_ptr<sharp_alm_info> set_triangular_alm_info(int64_t lmax, int64_t mmax) {
  MR_assert(mmax >= 0, "negative mmax");
  MR_assert(mmax <= lmax, "mmax must not be larger than lmax");
  return sharp_make_triangular_alm_info(lmax, mmax, 1);
}

/// Creates a new geometry information
sharp_standard_geom_info *GeometryInformation(size_t nrings, a_s &nph,
                                              a_li &ofs, ptrdiff_t stride,
                                              a_d &phi0, a_d &theta, a_d &wgt) {
  auto nph_p = nph.unchecked<1>();
  auto ofs_p = ofs.unchecked<1>();
  auto phi0_p = phi0.unchecked<1>();
  auto theta_p = theta.unchecked<1>();
  auto wgt_p = wgt.unchecked<1>();
  auto temp_p = make_unique<sharp_standard_geom_info>(
      nrings, &nph_p[0], &ofs_p[0], stride, &phi0_p[0], &theta_p[0], &wgt_p[0]);
  return temp_p.release();
}

/// Creates a new geometry info, keeping the rings only in zbounds
sharp_standard_geom_info *keep_rings_in_zbounds(sharp_standard_geom_info &ginfo,
                                                a_d &zbounds) {

  auto zb = zbounds.mutable_unchecked<1>();

  size_t nrings = ginfo.nrings();

  vector<size_t> nph;
  vector<ptrdiff_t> ofs;
  ptrdiff_t stride = 1;
  vector<double> phi0;
  vector<double> theta;
  vector<double> wgt;

  size_t iring = 0;
  size_t nrings_new = 0;
  for (; iring < nrings; ++iring) {
    if (cos(ginfo.theta(iring)) >= zb[0] && cos(ginfo.theta(iring)) <= zb[1]) {
      nrings_new += 1;
      // add ring info to new structure
      nph.push_back(ginfo.nph(iring));
      ofs.push_back(ginfo.ofs(iring));
      phi0.push_back(ginfo.phi0(iring));
      theta.push_back(ginfo.theta(iring));
      wgt.push_back(ginfo.weight(iring));
    }
  }
  return make_unique<sharp_standard_geom_info>(
             nrings_new, &nph[0], &ofs[0], stride, &phi0[0], &theta[0], &wgt[0])
      .release();
}

/// Creates a HEALPix geometry
sharp_standard_geom_info *sharp_make_standard_healpix_geom_info(size_t nside,
                                                                size_t stride) {
  return sharp_make_healpix_geom_info(nside, stride).release();
}

/// Creates a Gauss geometry
sharp_standard_geom_info *gauss_geometry(int64_t nrings, int64_t nphi) {
  MR_assert((nrings > 0) && (nphi > 0), "bad grid dimensions");
  sharp_standard_geom_info *ginfo =
      sharp_make_2d_geom_info(nrings, nphi, 0., 1, nphi, "GL").release();
  return ginfo;
}

/// Computes alm given a temperature map on a custom geometry
a_c_c map2alm_ginfo(sharp_standard_geom_info *ginfo, a_d_c map, size_t lmax,
                    optional<size_t> mmax_opt, size_t nthreads,
                    optional<a_d> zbounds) {

  size_t mmax = (mmax_opt.has_value()) ? mmax_opt.value() : lmax;

  a_d zb = (zbounds.has_value()) ? zbounds.value()
                                 : a_d(py::cast(vector<double>({-1, 1})));

  sharp_standard_geom_info *ginfo_new = keep_rings_in_zbounds(*ginfo, zb);

  // make triangular alm info
  unique_ptr<sharp_alm_info> ainfo = set_triangular_alm_info(lmax, mmax);

  int64_t n_alm = ((mmax + 1) * (mmax + 2)) / 2 + (mmax + 1) * (lmax - mmax);
  a_c_c alm(n_alm);
  auto mr = map.unchecked<1>();
  auto ar = alm.mutable_unchecked<1>();

  sharp_map2alm(&ar[0], &mr[0], *ginfo_new, *ainfo, SHARP_USE_WEIGHTS,
                nthreads);
  return alm;
}

/// Computes alm given a polarization map on a custom geometry
a_c_c map2alm_spin_ginfo(sharp_standard_geom_info *ginfo, const a_d_c &map,
                         int64_t spin, const int64_t lmax,
                         optional<int64_t> mmax_opt, const int nthreads,
                         optional<a_d> zbounds) {

  size_t mmax = (mmax_opt.has_value()) ? mmax_opt.value() : lmax;
  a_d zb = (zbounds.has_value()) ? zbounds.value()
                                 : a_d(py::cast(vector<double>({-1, 1})));

  sharp_standard_geom_info *ginfo_new = keep_rings_in_zbounds(*ginfo, zb);

  // make triangular alm info
  unique_ptr<sharp_alm_info> ainfo = set_triangular_alm_info(lmax, mmax);

  int64_t n_alm = ((mmax + 1) * (mmax + 2)) / 2 + (mmax + 1) * (lmax - mmax);
  a_c_c alm(vector<size_t>{2, size_t(n_alm)});
  auto ar = alm.mutable_unchecked<2>();

  int64_t npix = 0;
  for (size_t i = 0; i < ginfo->nrings(); ++i) {
    ;
    npix += ginfo->nph(i);
  }

  auto mr = map.unchecked<2>();
  MR_assert((mr.shape(0) == 2) && (mr.shape(1) == npix),
            "incorrect size of map array");

  sharp_map2alm_spin(spin, &ar(0, 0), &ar(1, 0), &mr(0, 0), &mr(1, 0),
                     *ginfo_new, *ainfo, SHARP_USE_WEIGHTS, nthreads);
  return alm;
}

/// Computes alm given temperature alm on a custom geometry
a_d_c alm2map_ginfo(sharp_standard_geom_info *ginfo, const a_c_c &alm,
                    const int64_t lmax, optional<int64_t> mmax_opt,
                    const int nthreads, optional<a_d> zbounds) {

  size_t mmax = (mmax_opt.has_value()) ? mmax_opt.value() : lmax;

  a_d zb = (zbounds.has_value()) ? zbounds.value()
                                 : a_d(py::cast(vector<double>({-1, 1})));

  sharp_standard_geom_info *ginfo_new = keep_rings_in_zbounds(*ginfo, zb);

  // make triangular alm info
  unique_ptr<sharp_alm_info> ainfo = set_triangular_alm_info(lmax, mmax);

  int64_t n_alm = ((mmax + 1) * (mmax + 2)) / 2 + (mmax + 1) * (lmax - mmax);
  MR_assert(alm.size() == n_alm, "incorrect size of a_lm array");

  size_t npix = 0;
  for (size_t i = 0; i < ginfo->nrings(); ++i) {
    ;
    npix += ginfo->nph(i);
  }

  a_d_c map(npix, 0);
  auto mr = map.mutable_unchecked<1>();

  for (size_t i = 0; i < npix; ++i) {
    mr[i] = 0;
  }

  auto ar = alm.unchecked<1>();

  sharp_alm2map(&ar[0], &mr[0], *ginfo_new, *ainfo, 0, nthreads);
  return map;
}

/// Computes polarization map given alms on a custom geometry
a_d_c alm2map_spin_ginfo(sharp_standard_geom_info *ginfo, const a_c_c &alm,
                         int64_t spin, const int64_t lmax,
                         optional<int64_t> mmax_opt, const int nthreads,
                         optional<a_d> zbounds) {

  size_t mmax = (mmax_opt.has_value()) ? mmax_opt.value() : lmax;
  a_d zb = (zbounds.has_value()) ? zbounds.value()
                                 : a_d(py::cast(vector<double>({-1, 1})));

  sharp_standard_geom_info *ginfo_new = keep_rings_in_zbounds(*ginfo, zb);

  // make triangular alm info
  unique_ptr<sharp_alm_info> ainfo = set_triangular_alm_info(lmax, mmax);

  auto ar = alm.unchecked<2>();
  int64_t n_alm = ((mmax + 1) * (mmax + 2)) / 2 + (mmax + 1) * (lmax - mmax);
  MR_assert((ar.shape(0) == 2) && (ar.shape(1) == n_alm),
            "incorrect size of a_lm array");

  int64_t npix = 0;
  for (size_t i = 0; i < ginfo->nrings(); ++i) {
    ;
    npix += ginfo->nph(i);
  }

  a_d_c map(vector<size_t>{2, size_t(npix)});
  auto mr = map.mutable_unchecked<2>();
  sharp_alm2map_spin(spin, &ar(0, 0), &ar(1, 0), &mr(0, 0), &mr(1, 0),
                     *ginfo_new, *ainfo, 0, nthreads);
  return map;
}

/* SHT functions for HEALPix maps */

a_c_c map2alm(const a_d_c &map, const int64_t lmax,
              const optional<int64_t> mmax, const int nthreads,
              const optional<a_d> &zbounds) {

  const int64_t npix = map.ndim() == 1 ? map.size() : map.shape(1);
  const int64_t nside = (int)sqrt(npix / 12);
  sharp_standard_geom_info *ginfo =
      sharp_make_healpix_geom_info(nside, 1).release();
  return map2alm_ginfo(ginfo, map, lmax, mmax, nthreads, zbounds);
}

a_c_c map2alm_spin(const a_d_c &map, int64_t spin, const int64_t lmax,
                   const optional<int64_t> mmax, const int nthreads,
                   optional<a_d> zbounds) {

  const int64_t npix = map.ndim() == 1 ? map.size() : map.shape(1);
  const int64_t nside = (int)sqrt(npix / 12);
  sharp_standard_geom_info *ginfo =
      sharp_make_healpix_geom_info(nside, 1).release();
  return map2alm_spin_ginfo(ginfo, map, spin, lmax, mmax, nthreads, zbounds);
}

a_d_c alm2map(const a_c_c &alm, const int64_t nside, const int64_t lmax,
              const optional<int64_t> mmax, const int nthreads,
              optional<a_d> zbounds) {

  sharp_standard_geom_info *ginfo =
      sharp_make_healpix_geom_info(nside, 1).release();
  return alm2map_ginfo(ginfo, alm, lmax, mmax, nthreads, zbounds);
}

a_d_c alm2map_spin(const a_c_c &alm, int64_t spin, const int64_t nside,
                   const int64_t lmax, const optional<int64_t> mmax,
                   const int nthreads, optional<a_d> zbounds) {

  sharp_standard_geom_info *ginfo =
      sharp_make_healpix_geom_info(nside, 1).release();
  return alm2map_spin_ginfo(ginfo, alm, spin, lmax, mmax, nthreads, zbounds);
}

/* GL functions */

/// Computes Gauss-Legendre quadrature weights.
py::array GL_wg(size_t n) {
  auto res = make_Pyarr<double>({n});
  auto res2 = to_mav<double, 1>(res, true);
  GL_Integrator integ(n);
  auto wgt = integ.weights();
  for (size_t i = 0; i < res2.shape(0); ++i)
    res2.v(i) = wgt[i];
  return move(res);
}

/// Computes Gauss-Legendre quadrature sample points. Output is ordered from 1 to -1.
py::array GL_xg(size_t n) {
  auto res = make_Pyarr<double>({n});
  auto res2 = to_mav<double, 1>(res, true);
  GL_Integrator integ(n);
  auto x = integ.coords();
  for (size_t i = 0; i < res2.shape(0); ++i)
    res2.v(i) = -x[i];
  return move(res);
}

/* Phase functions */

a_c_c alm2phase_ginfo(sharp_standard_geom_info *ginfo, const a_c_c &alm,
                      const int64_t lmax, const int64_t mmax,
                      const int nthreads, optional<a_d> zbounds) {

  a_d zb = (zbounds.has_value()) ? zbounds.value()
                                 : a_d(py::cast(vector<double>({-1, 1})));

  sharp_standard_geom_info *ginfo_new = keep_rings_in_zbounds(*ginfo, zb);

  // make triangular alm info
  unique_ptr<sharp_alm_info> ainfo = set_triangular_alm_info(lmax, mmax);

  int64_t n_alm = ((mmax + 1) * (mmax + 2)) / 2 + (mmax + 1) * (lmax - mmax);
  MR_assert(alm.size() == n_alm, "incorrect size of a_lm array");

  size_t npix = 0;
  for (size_t i = 0; i < ginfo->nrings(); ++i) {
    ;
    npix += ginfo->nph(i);
  }

  long unsigned int nchunks;
  long unsigned int chunksize;
  get_singular_chunk_info(ginfo_new->npairs(), (0 == 0) ? 128 : 64, nchunks,
                          chunksize);

  auto none_array = py::none();
  auto phase_a2p = get_optional_Pyarr<complex<double>>(
      none_array, {1, 2 * chunksize, unsigned(mmax) + 1});
  auto phase_mav_a2p = to_mav<complex<double>, 3>(phase_a2p, true);

  py::buffer_info buf = phase_a2p.request();
  auto *ptr = static_cast<complex<double> *>(buf.ptr);

  // for safety reasons
  for (size_t i = 0; i < 2 * chunksize * (mmax + 1) * 1; ++i) {
    ptr[i] = std::complex<double>(0);
  }

  auto ar = alm.unchecked<1>();

  sharp_alm2phase(&ar[0], phase_mav_a2p, *ginfo_new, *ainfo, 0, nthreads);
  long unsigned int newshape[2] = {2 * chunksize, unsigned(mmax + 1)};
  phase_a2p.resize(newshape);
  return phase_a2p;
}

a_c_c phase2alm_ginfo(sharp_standard_geom_info *ginfo, a_c_c &phase_p2a,
                      size_t lmax, size_t mmax, size_t nthreads,
                      optional<a_d> zbounds) {

  a_d zb = (zbounds.has_value()) ? zbounds.value()
                                 : a_d(py::cast(vector<double>({-1, 1})));

  sharp_standard_geom_info *ginfo_new = keep_rings_in_zbounds(*ginfo, zb);

  long unsigned int newshape[3] = {1, unsigned(phase_p2a.shape(0)),
                                   unsigned(phase_p2a.shape(1))};
  long unsigned int oldshape[2] = {unsigned(phase_p2a.shape(0)),
                                   unsigned(phase_p2a.shape(1))};
  phase_p2a.resize(newshape);

  unique_ptr<sharp_alm_info> ainfo = set_triangular_alm_info(lmax, mmax);

  int64_t n_alm = ((mmax + 1) * (mmax + 2)) / 2 + (mmax + 1) * (lmax - mmax);
  a_c_c alm(n_alm);
  auto ar = alm.mutable_unchecked<1>();
  auto phase_mav_p2a = to_mav<complex<double>, 3>(
      phase_p2a, true);

  sharp_phase2alm(&ar[0], phase_mav_p2a, *ginfo_new, *ainfo, SHARP_USE_WEIGHTS,
                  nthreads);
  phase_p2a.resize(oldshape);
  return alm;
}

a_d_c phase2map_ginfo(sharp_standard_geom_info *ginfo, a_c_c &phase,
                      size_t lmax, size_t mmax, size_t nthreads,
                      optional<a_d> zbounds) {

  a_d zb = (zbounds.has_value()) ? zbounds.value()
                                 : a_d(py::cast(vector<double>({-1, 1})));
  sharp_standard_geom_info *ginfo_new = keep_rings_in_zbounds(*ginfo, zb);

  unique_ptr<sharp_alm_info> ainfo = set_triangular_alm_info(lmax, mmax);

  size_t npix = 0;
  for (size_t i = 0; i < ginfo->nrings(); ++i) {
    ;
    npix += ginfo->nph(i);
  }

  if (phase.ndim() > 2) {

    int64_t n_alm = ((mmax + 1) * (mmax + 2)) / 2 + (mmax + 1) * (lmax - mmax);
    a_c_c alm(vector<size_t>{2, size_t(n_alm)});
    auto ar = alm.mutable_unchecked<2>();

    auto phase_mav = to_mav<complex<double>, 3>(phase, true);
    a_d_c map(vector<size_t>{2, size_t(npix)});
    auto mr = map.mutable_unchecked<2>();

    // see: spin = 0 ?
    phase_job job(SHARP_Y, 2, {&ar(0, 0), &ar(1, 0)}, {&mr(0, 0), &mr(1, 0)},
                  phase_mav, *ginfo_new, *ainfo, 0, nthreads);
    phase_execute_phase2map(job, phase_mav, *ginfo_new, mmax, 2);
    return map;
  }

  long unsigned int newshape[3] = {1, unsigned(phase.shape(0)),
                                   unsigned(phase.shape(1))};
  long unsigned int oldshape[2] = {unsigned(phase.shape(0)),
                                   unsigned(phase.shape(1))};
  phase.resize(newshape);

  int64_t n_alm = ((mmax + 1) * (mmax + 2)) / 2 + (mmax + 1) * (lmax - mmax);
  a_c_c alm(n_alm);
  auto ar = alm.mutable_unchecked<1>();
  auto phase_mav = to_mav<complex<double>, 3>(phase, true);

  a_d_c map(npix);
  auto mr = map.mutable_unchecked<1>();

  for (size_t i = 0; i < npix; ++i) {
    mr[i] = 0;
  }

  phase_job job(SHARP_Y, 0, {&ar[0]}, {&mr[0]}, phase_mav, *ginfo_new, *ainfo,
                0, nthreads);
  phase_execute_phase2map(job, phase_mav, *ginfo_new, mmax, 0);
  phase.resize(oldshape);
  return map;
}

a_c_c map2phase_ginfo(sharp_standard_geom_info *ginfo, a_d_c &map, size_t lmax,
                      size_t mmax, size_t nthreads, optional<a_d> zbounds) {

  a_d zb = (zbounds.has_value()) ? zbounds.value()
                                 : a_d(py::cast(vector<double>({-1, 1})));
  sharp_standard_geom_info *ginfo_new = keep_rings_in_zbounds(*ginfo, zb);

  unique_ptr<sharp_alm_info> ainfo = set_triangular_alm_info(lmax, mmax);

  if (map.ndim() > 1) {
    auto mr = map.mutable_unchecked<2>();

    int64_t n_alm = ((mmax + 1) * (mmax + 2)) / 2 + (mmax + 1) * (lmax - mmax);
    a_c_c alm(vector<size_t>{2, size_t(n_alm)});
    auto ar = alm.mutable_unchecked<2>();
    long unsigned int nchunks;
    long unsigned int chunksize;
    get_singular_chunk_info(ginfo_new->npairs(), (0 == 0) ? 128 : 64, nchunks,
                            chunksize);

    auto none_array = py::none();
    auto phase_m2p = get_optional_Pyarr<complex<double>>(
        none_array, {unsigned(map.ndim()), 2 * chunksize, unsigned(mmax) + 1});
    auto phase_mav = to_mav<complex<double>, 3>(phase_m2p, true);

    py::buffer_info buf = phase_m2p.request();
    auto *ptr = static_cast<complex<double> *>(buf.ptr);

    // for safety reasons
    for (size_t i = 0; i < 2 * chunksize * (mmax + 1) * 1; ++i) {
      ptr[i] = std::complex<double>(0);
    }

    phase_job job(SHARP_Yt, 2, {&ar(0, 0), &ar(1, 0)}, {&mr(0, 0), &mr(1, 0)},
                  phase_mav, *ginfo_new, *ainfo, SHARP_USE_WEIGHTS, nthreads);
    job.type = SHARP_MAP2ALM;
    phase_execute_map2phase(job, phase_mav, *ginfo_new, mmax, 2);
    return phase_m2p;
  }
  auto mr = map.mutable_unchecked<1>();

  int64_t n_alm = ((mmax + 1) * (mmax + 2)) / 2 + (mmax + 1) * (lmax - mmax);
  a_c_c alm(n_alm);
  auto ar = alm.mutable_unchecked<1>();
  long unsigned int nchunks;
  long unsigned int chunksize;
  get_singular_chunk_info(ginfo_new->npairs(), (0 == 0) ? 128 : 64, nchunks,
                          chunksize);

  auto none_array = py::none();
  auto phase_m2p = get_optional_Pyarr<complex<double>>(
      none_array, {unsigned(map.ndim()), 2 * chunksize, unsigned(mmax) + 1});
  auto phase_mav = to_mav<complex<double>, 3>(phase_m2p, true);

  py::buffer_info buf = phase_m2p.request();
  auto *ptr = static_cast<complex<double> *>(buf.ptr);

  // for safety reasons
  for (size_t i = 0; i < 2 * chunksize * (mmax + 1) * 1; ++i) {
    ptr[i] = std::complex<double>(0);
  }

  phase_job job(SHARP_Yt, 0, {&ar[0]}, {&mr[0]}, phase_mav, *ginfo_new, *ainfo,
                SHARP_USE_WEIGHTS, nthreads);
  job.type = SHARP_MAP2ALM;
  phase_execute_map2phase(job, phase_mav, *ginfo_new, mmax, 0);
  long unsigned int newshape[2] = {unsigned(phase_m2p.shape(1)),
                                   unsigned(phase_m2p.shape(2))};
  phase_m2p.resize(newshape);
  return phase_m2p;
}

a_c_c alm2phase_spin_ginfo(sharp_standard_geom_info *ginfo, const a_c_c &alm,
                           const size_t spin, const int64_t lmax,
                           const int64_t mmax, const int nthreads,
                           optional<a_d> zbounds) {

  a_d zb = (zbounds.has_value()) ? zbounds.value()
                                 : a_d(py::cast(vector<double>({-1, 1})));
  sharp_standard_geom_info *ginfo_new = keep_rings_in_zbounds(*ginfo, zb);

  // make triangular alm info
  unique_ptr<sharp_alm_info> ainfo = set_triangular_alm_info(lmax, mmax);

  int64_t n_alm = ((mmax + 1) * (mmax + 2)) / 2 + (mmax + 1) * (lmax - mmax);

  size_t npix = 0;
  for (size_t i = 0; i < ginfo->nrings(); ++i) {
    ;
    npix += ginfo->nph(i);
  }

  long unsigned int nchunks;
  long unsigned int chunksize;
  get_singular_chunk_info(ginfo_new->npairs(), (0 == 0) ? 128 : 64, nchunks,
                          chunksize);

  auto none_array = py::none();
  auto phase = get_optional_Pyarr<complex<double>>(
      none_array, {2, 2 * chunksize, unsigned(mmax) + 1});
  auto phase_mav = to_mav<complex<double>, 3>(phase, true);

  auto ar = alm.unchecked<2>();
  MR_assert((ar.shape(0) == 2) && (ar.shape(1) == n_alm),
            "incorrect size of a_lm array");

  sharp_alm2phase_spin(spin, &ar(0, 0), &ar(1, 0), phase_mav, *ginfo_new,
                       *ainfo, 0, nthreads);
  return phase;
}

a_c_c phase2alm_spin_ginfo(sharp_standard_geom_info *ginfo, a_c_c &phase,
                           size_t spin, size_t lmax, size_t mmax,
                           size_t nthreads, optional<a_d> zbounds) {

  a_d zb = (zbounds.has_value()) ? zbounds.value()
                                 : a_d(py::cast(vector<double>({-1, 1})));
  sharp_standard_geom_info *ginfo_new = keep_rings_in_zbounds(*ginfo, zb);

  unique_ptr<sharp_alm_info> ainfo = set_triangular_alm_info(lmax, mmax);

  int64_t n_alm = ((mmax + 1) * (mmax + 2)) / 2 + (mmax + 1) * (lmax - mmax);
  a_c_c alm(vector<size_t>{2, size_t(n_alm)});
  auto ar = alm.mutable_unchecked<2>();
  auto phase_mav = to_mav<complex<double>, 3>(phase);

  sharp_phase2alm_spin(spin, &ar(0, 0), &ar(1, 0), phase_mav, *ginfo_new,
                       *ainfo, SHARP_USE_WEIGHTS, nthreads);
  return alm;
}
