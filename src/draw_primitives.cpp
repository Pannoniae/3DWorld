// 3D World - Shape primitive drawing
// by Frank Gennari
// 3/6/06
#include "function_registry.h"
#include "subdiv.h"
#include "upsurface.h"
#include "gl_ext_arb.h"
#include "shaders.h"
#include "draw_utils.h"
#include "transform_obj.h"


// predefined sphere VBOs
bool const USE_SPHERE_VBOS = 1;
unsigned const NUM_PREDEF_SPHERES(4*N_SPHERE_DIV);

unsigned sphere_vbo_offsets[NUM_PREDEF_SPHERES+1] = {0};
unsigned predef_sphere_vbo(0);
vector_point_norm cylinder_vpn;


extern int display_mode, draw_model;


#define NDIV_SCALE(val) unsigned(val*ndiv + 0.5)


// ******************** CURVE GENERATORS ********************


void get_ortho_vectors(vector3d const &v12, vector3d *vab, int force_dim) {

	assert(vab != NULL);
	vector3d vtest(v12);
	unsigned dim(0);

	if (force_dim >= 0) {
		assert(force_dim < 3);
		dim = force_dim;
	}
	else {
		dim = get_min_dim(v12);
	}
	vtest[dim] += 0.5;
	cross_product(vtest, v12,    vab[0]); // vab[0] is orthogonal to v12
	cross_product(v12,   vab[0], vab[1]); // vab[1] is orthogonal to v12 and vab[0]
	for (unsigned i = 0; i < 2; ++i) vab[i].normalize();
}


// create class sd_cylin_d?
// perturb_map usage is untested
vector_point_norm const &gen_cylinder_data(point const ce[2], float radius1, float radius2, unsigned ndiv, vector3d &v12,
										   float const *const perturb_map, float s_beg, float s_end, int force_dim)
{
	assert(ndiv > 0 && ndiv < 100000);
	v12 = (ce[1] - ce[0]).get_norm();
	float const r[2] = {radius1, radius2};
	vector3d vab[2];
	get_ortho_vectors(v12, vab, force_dim);
	vector_point_norm &vpn(cylinder_vpn);
	unsigned s0(NDIV_SCALE(s_beg)), s1(NDIV_SCALE(s_end));
	if (s1 == ndiv) s0 = 0; // make wraparound correct
	s1 = min(ndiv, s1+1);   // allow for sn
	vpn.p.resize(2*ndiv);
	vpn.n.resize(ndiv);
	float const css(TWO_PI/(float)ndiv), sin_ds(sin(css)), cos_ds(cos(css));
	float sin_s((s0 == 0) ? 0.0 : sin(s0*css)), cos_s((s0 == 0) ? 1.0 : cos(s0*css));
	// sin(x + y) = sin(x)*cos(y) + cos(x)*sin(y)
	// cos(x + y) = cos(x)*cos(y) - sin(x)*sin(y)

	for (unsigned S = s0; S < s1; ++S) { // build points table
		float const s(sin_s), c(cos_s);
		vpn.p[(S<<1)+0] = ce[0] + (vab[0]*(r[0]*s)) + (vab[1]*(r[0]*c)); // loop unrolled
		vpn.p[(S<<1)+1] = ce[1] + (vab[0]*(r[1]*s)) + (vab[1]*(r[1]*c));
		sin_s = s*cos_ds + c*sin_ds;
		cos_s = c*cos_ds - s*sin_ds;
	}
	float nmag_inv(0.0);
	bool const npt_r2(radius1 < radius2); // determine normal from longest edge for highest accuracy (required for when r1 == 0.0)

	for (unsigned S = s0; S < s1; ++S) { // build normals table
		vector3d const v1(vpn.p[(((S+1)%ndiv)<<1)+npt_r2], vpn.p[(S<<1)+npt_r2]), v2(vpn.p[(S<<1)+1], vpn.p[S<<1]);
		cross_product(v2, v1, vpn.n[S]);
		if (S == s0) nmag_inv = 1.0/vpn.n[S].mag(); // first one (should not have a divide by zero)
		vpn.n[S] *= nmag_inv; //norms[S].normalize();
	}
	if (perturb_map != NULL) { // add in perturbations
		float const ravg(0.5*(r[0] + r[1]));
		float const pscale[2] = {r[0]/ravg, r[1]/ravg};

		for (unsigned S = s0; S < s1; ++S) {
			for (unsigned i = 0; i < 2; ++i) {
				vpn.p[(S<<1)+i] += vpn.n[S]*(pscale[i]*perturb_map[S]);
			}
		}
		// update normals?
	}
	return vpn;
}


// *** sd_sphere_d ***


void sd_sphere_d::gen_points_norms_static(float s_beg, float s_end, float t_beg, float t_end) {

	static sphere_point_norm temp_spn;
	gen_points_norms(temp_spn, s_beg, s_end, t_beg, t_end);
}


void sd_sphere_d::gen_points_norms(sphere_point_norm &cur_spn, float s_beg, float s_end, float t_beg, float t_end) {

	unsigned const ndiv(spn.ndiv);
	assert(ndiv > 0 && ndiv < 1000); // FIXME: should we refuse to accept (ndiv < 3) ?

	if (cur_spn.points == NULL || ndiv > cur_spn.ndiv) { // allocate all memory
		if (cur_spn.points != NULL) cur_spn.free_data(); // already allocated at least once
		cur_spn.alloc(ndiv);
	}
	else {
		cur_spn.set_pointer_stride(ndiv);
	}
	float const cs_scale(PI/(float)ndiv), cs_scale2(2.0*cs_scale), sin_dt(sin(cs_scale)), cos_dt(cos(cs_scale));
	unsigned s0(NDIV_SCALE(s_beg)), s1(NDIV_SCALE(s_end)), t0(NDIV_SCALE(t_beg)), t1(NDIV_SCALE(t_end));
	if (s1 == ndiv) s0 = 0; // make wraparound correct
	s1 = min(ndiv, s1+1);   // allow for sn
	t1 = min(ndiv, t1+1);   // allow for tn
	float sin_t0((t0 == 0) ? 0.0 : sin(t0*cs_scale)), cos_t0((t0 == 0) ? 1.0 : cos(t0*cs_scale));

	for (unsigned s = s0; s < s1; ++s) { // build points and normals table
		float const theta(s*cs_scale2), tvc(cos(theta)), tvs(sin(theta)); // theta1, theta2
		float sin_t(sin_t0), cos_t(cos_t0);
		unsigned const soff((ndiv+1)*s);

		for (unsigned t = t0; t <= t1; ++t) { // Note: x and y are swapped because theta is out of phase by 90 degrees to match tex coords
			point &pt(cur_spn.points[s][t]);
			float const sv(sin_t), cv(cos_t);
			pt.assign(sv*tvs, sv*tvc, cv); // R*sin(phi)*sin(theta), R*sin(phi)*cos(theta), R*cos(phi)
			sin_t = sv*cos_dt + cv*sin_dt;
			cos_t = cv*cos_dt - sv*sin_dt;
			cur_spn.norms[s][t] = pt; // Note: perturb_map does not affect normals until later
			pt   *= radius;
			pt   += pos;
			if (perturb_map) pt += cur_spn.norms[s][t]*perturb_map[t+soff];
			if (surf)        pt += cur_spn.norms[s][t]*surf->get_height_at(pt);
		}
	}
	if (perturb_map || surf) { // recalculate vertex/surface normals
		for (unsigned s = s0; s < s1; ++s) {
			for (unsigned t = max(1U, t0); t <= min(t1, ndiv-1); ++t) { // skip the poles
				point const p(cur_spn.points[s][t]);
				vector3d n(zero_vector);

				for (unsigned d = 0; d < 4; ++d) { // s+,t+  t-,s+  s-,t-  t+,s-
					unsigned const si((d==0||d==1) ? (s+1)%ndiv : (s+ndiv-1)%ndiv);
					unsigned const ti((d==0||d==3) ? (t+1)      : (t-1));
					vector3d const nst(cross_product((cur_spn.points[si][t] - p), (cur_spn.points[s][ti] - p)));
					float const nmag(nst.mag());
					if (nmag > TOLERANCE) n += nst*(((d&1) ? -0.25 : 0.25)/nmag);
				}
				cur_spn.norms[s][t] = n;
			}
		}
	}
	spn.points = cur_spn.points;
	spn.norms  = cur_spn.norms;
}


float sd_sphere_d::get_rmax() const { // could calculate this during gen_points_norms

	assert(spn.points);
	float rmax_sq(0.0);

	for (unsigned y = 0; y < spn.ndiv; ++y) {
		for (unsigned x = 0; x <= spn.ndiv; ++x) {
			rmax_sq = max(rmax_sq, p2p_dist_sq(pos, spn.points[y][x]));
		}
	}
	return sqrt(rmax_sq);
}


void sd_sphere_d::set_data(point const &p, float r, int n, float const *pm, float dp, upsurface const *const s) {

	pos         = p;
	radius      = r;
	def_pert    = dp;
	perturb_map = pm;
	surf        = s;
	spn.ndiv    = n;
	assert(radius > 0.0);
}


// *** sphere_point_norm ***


void sphere_point_norm::alloc(unsigned ndiv_) {

	ndiv = ndiv_;
	matrix_base_alloc_2d(points, (ndiv+1), 2*ndiv);
	set_pointer_stride(ndiv);
}


void sphere_point_norm::set_pointer_stride(unsigned ndiv_) {

	ndiv     = ndiv_;
	norms    = points    + ndiv; // assumes point and vector3d are the same
	norms[0] = points[0] + ndiv*(ndiv+1); // num_alloc
	matrix_ptr_fill_2d(points, (ndiv+1), ndiv);
	matrix_ptr_fill_2d(norms,  (ndiv+1), ndiv);
}


void sphere_point_norm::free_data() {

	if (points == NULL) {assert(norms == NULL); return;} // already freed (okay)
	assert(points && norms && points[0] && norms[0]);
	matrix_delete_2d(points);
	norms = NULL;
}


// ******************** CYLINDER ********************


void draw_circle_normal(float r_inner, float r_outer, int ndiv, int invert_normals, float zval) {

	assert(r_outer > 0.0);
	bool const disk(r_inner > 0.0);
	vector3d const n(invert_normals ? -plus_z : plus_z);
	float const css((invert_normals ? 1.0 : -1.0)*TWO_PI/(float)ndiv), sin_ds(sin(css)), cos_ds(cos(css));
	float const inner_tscale(r_inner/r_outer);
	float sin_s(0.0), cos_s(1.0);
	static vector<vert_norm_tc> verts;
	if (!disk) {verts.push_back(vert_norm_tc(point(0.0, 0.0, zval), n, 0.5, 0.5));}

	for (unsigned S = 0; S <= (unsigned)ndiv; ++S) {
		float const s(sin_s), c(cos_s);
		if (disk) {verts.push_back(vert_norm_tc(point(r_inner*s, r_inner*c, zval), n, 0.5*(1.0 + inner_tscale*s), (0.5*(1.0 + inner_tscale*c))));}
		verts.push_back(vert_norm_tc(point(r_outer*s, r_outer*c, zval), n, 0.5*(1.0 + s), (0.5*(1.0 + c))));
		sin_s = s*cos_ds + c*sin_ds;
		cos_s = c*cos_ds - s*sin_ds;
	}
	draw_and_clear_verts(verts, (disk ? GL_TRIANGLE_STRIP : GL_TRIANGLE_FAN));
}


// length can be negative
void draw_cylinder(float length, float radius1, float radius2, int ndiv, bool draw_ends, bool first_end_only, bool last_end_only, float z_offset) {
	
	assert(ndiv > 0 );
	draw_cylin_fast(radius1, radius2, length, ndiv, 1, 1.0, z_offset); // tex coords?
	if (draw_ends && !last_end_only  && radius1 > 0.0) {draw_circle_normal(0.0, radius1, ndiv, 1, z_offset);}
	if (draw_ends && !first_end_only && radius2 > 0.0) {draw_circle_normal(0.0, radius2, ndiv, 0, z_offset+length);}
}


void draw_cylinder(point const &p1, float length, float radius1, float radius2, int ndiv, bool draw_ends) {

	glPushMatrix();
	translate_to(p1);
	draw_cylinder(length, radius1, radius2, ndiv, draw_ends);
	glPopMatrix();
}


// Note: two_sided_lighting is not entirely correct since it operates on the vertices instead of the faces/fragments
vert_norm_tc create_vert(point const &p, vector3d const &n, float ts, float tt, bool two_sided_lighting) {
	return vert_norm_tc(p, ((two_sided_lighting && dot_product_ptv(n, get_camera_pos(), p) < 0.0) ? -n : n), ts, tt);
}


// draw_sides_ends: 0 = draw sides only, 1 = draw sides and ends, 2 = draw ends only, 3 = pt1 end, 4 = pt2 end
void draw_fast_cylinder(point const &p1, point const &p2, float radius1, float radius2, int ndiv, bool texture,
						int draw_sides_ends, bool two_sided_lighting, float const *const perturb_map, float tex_scale_len)
{
	assert(radius1 > 0.0 || radius2 > 0.0);
	point const ce[2] = {p1, p2};
	float const ndiv_inv(1.0/ndiv);
	vector3d v12; // (ce[1] - ce[0]).get_norm()
	vector_point_norm const &vpn(gen_cylinder_data(ce, radius1, radius2, ndiv, v12, perturb_map));
	static vector<vert_norm_tc> verts;

	if (draw_sides_ends == 2) {
		// draw ends only - nothing to do here
	}
	else if (radius2 == 0.0) { // cone (Note: still not perfect for pine tree trunks and enforcer ships)
		verts.resize(3*ndiv);

		for (unsigned s = 0; s < (unsigned)ndiv; ++s) { // Note: always has tex coords
			unsigned const sp((s+ndiv-1)%ndiv), sn((s+1)%ndiv);
			//verts[3*s+0] = create_vert(vpn.p[(s <<1)+1], vpn.n[s], (1.0 - (s+0.5)*ndiv_inv), tex_scale_len, two_sided_lighting); // small discontinuities at every position
			verts[3*s+0] = create_vert(vpn.p[(s <<1)+1], vpn.n[s], 0.5, tex_scale_len, two_sided_lighting); // one big discontinuity at one position
			verts[3*s+1] = create_vert(vpn.p[(sn<<1)+0], (vpn.n[s] + vpn.n[sn]), (1.0 - (s+1.0)*ndiv_inv), 0.0, two_sided_lighting); // normalize?
			verts[3*s+2] = create_vert(vpn.p[(s <<1)+0], (vpn.n[s] + vpn.n[sp]), (1.0 - (s+0.0)*ndiv_inv), 0.0, two_sided_lighting); // normalize?
		}
		draw_and_clear_verts(verts, GL_TRIANGLES);
	}
	else {
		verts.resize(2*(ndiv+1));

		for (unsigned S = 0; S <= (unsigned)ndiv; ++S) { // Note: always has tex coords
			unsigned const s(S%ndiv);
			vector3d const normal(vpn.n[s] + vpn.n[(S+ndiv-1)%ndiv]); // normalize?
			verts[2*S+0] = create_vert(vpn.p[(s<<1)+0], normal, (1.0 - S*ndiv_inv), 0.0, two_sided_lighting);
			verts[2*S+1] = create_vert(vpn.p[(s<<1)+1], normal, (1.0 - S*ndiv_inv), tex_scale_len, two_sided_lighting);
		}
		draw_and_clear_verts(verts, GL_TRIANGLE_STRIP);
	}
	if (draw_sides_ends != 0) { // Note: two_sided_lighting doesn't apply here
		float const r[2] = {radius1, radius2};

		for (unsigned i = 0; i < 2; ++i) {
			if (r[i] == 0.0 || (draw_sides_ends == 3+(!i))) continue;
			vector3d const normal(i ? v12 : -v12);
			verts.push_back(vert_norm_tc(ce[i], normal, 0.5, 0.5));

			for (unsigned S = 0; S <= (unsigned)ndiv; ++S) {
				unsigned const ss(S%ndiv), s(i ? (ndiv - ss - 1) : ss);
				float tc[2] = {0.0, 0.0};
				
				if (texture) { // inefficient, but uncommon
					float const theta(TWO_PI*s*ndiv_inv);
					tc[0] = 0.5*(1.0 + sinf(theta));
					tc[1] = 0.5*(1.0 + cosf(theta));
				}
				verts.push_back(vert_norm_tc(vpn.p[(s<<1)+i], normal, tc));
			}
			draw_and_clear_verts(verts, GL_TRIANGLE_FAN);
		}
	}
}


void draw_cylin_fast(float r1, float r2, float l, int ndiv, bool texture, float tex_scale_len, float z_offset) {

	draw_fast_cylinder(point(0.0, 0.0, z_offset), point(0.0, 0.0, z_offset+l), r1, r2, ndiv, texture, 0, 0, NULL, tex_scale_len);
}


void draw_cylindrical_section(float length, float r_inner, float r_outer, int ndiv, bool texture, float tex_scale_len, float z_offset) {

	assert(r_outer > 0.0 && r_inner >= 0.0 && length >= 0.0 && ndiv > 0 && r_outer >= r_inner);
	draw_cylin_fast(r_outer, r_outer, length, ndiv, texture, tex_scale_len, z_offset);

	if (r_inner != r_outer) {
		if (r_inner != 0.0) {draw_cylin_fast(r_inner, r_inner, length, ndiv, texture, tex_scale_len, z_offset);}
		draw_circle_normal(r_inner, r_outer, ndiv, 1, z_offset);
		draw_circle_normal(r_inner, r_outer, ndiv, 0, z_offset+length);
	}
}


// ******************** SPHERE ********************


// back face culling requires the sphere to be untransformed (or the vertices to be per-transformed)
void sd_sphere_d::draw_subdiv_sphere(point const &vfrom, int texture, bool disable_bfc, bool const *const render_map,
									 float const *const exp_map, point const *const pt_shift, float expand,
									 float s_beg, float s_end, float t_beg, float t_end) const
{
	assert(!render_map || disable_bfc);
	unsigned const ndiv(spn.ndiv);
	assert(ndiv > 0);
	float const ndiv_inv(1.0/float(ndiv)), rv(def_pert + radius), rv_sq(rv*rv), tscale(texture);
	float const toler(1.0E-6*radius*radius + rv_sq), dmax(rv + 0.1*radius), dmax_sq(dmax*dmax);
	point **points   = spn.points;
	vector3d **norms = spn.norms;
	bool const use_quads(render_map || pt_shift || exp_map || expand != 0.0);
	if (expand != 0.0) expand *= 0.25; // 1/4, for normalization
	unsigned const s0(NDIV_SCALE(s_beg)), s1(NDIV_SCALE(s_end)), t0(NDIV_SCALE(t_beg)), t1(NDIV_SCALE(t_end));
	static vector<vert_norm> vn;
	static vector<vertex_type_t> vntc;

	for (unsigned s = s0; s < s1; ++s) {
		s = min(s, s1-1);
		unsigned const sn((s+1)%ndiv), snt(s+1);

		if (use_quads) { // use slower quads - no back face culling
			for (unsigned t = t0; t < t1; ++t) {
				t = min(t, t1);
				unsigned const ix(s*(ndiv+1)+t), tn(t+1);
				if (render_map && !render_map[ix]) continue;
				float const exp(expand + (exp_map ? exp_map[ix] : 0.0));
				point          pts[4]     = {points[s][t], points[sn][t], points[sn][tn], points[s][tn]};
				vector3d const normals[4] = {norms [s][t], norms [sn][t], norms [sn][tn], norms [s][tn]};
				
				if (exp != 0.0) { // average the normals
					vector3d const quad_norm(normals[0] + normals[1] + normals[2] + normals[3]);
					for (unsigned i = 0; i < 4; ++i) {pts[i] += quad_norm*exp;}
				}
				for (unsigned i = 0; i < 4; ++i) {
					if (pt_shift) {pts[i] += pt_shift[ix];}

					if (texture) {
						vntc.push_back(vertex_type_t(pts[i], normals[i], tscale*(1.0f - (((i&1)^(i>>1)) ? snt : s)*ndiv_inv), tscale*(1.0f - ((i>>1) ? tn : t)*ndiv_inv)));
					}
					else {
						vn.push_back(vert_norm(pts[i], normals[i]));
					}
				}
			} // for t
		}
		else { // use triangle strip
			if (s != s0) { // add degenerate triangle to preserve the triangle strip (only slightly faster than using multiple triangle strips)
				for (unsigned d = 0; d < 2; ++d) {
					if (texture) {
						vntc.push_back(vertex_type_t(points[s][t0], zero_vector, 0.0, 0.0));
					}
					else {
						vn.push_back(vert_norm(points[s][t0], zero_vector));
					}
				}
			}
			for (unsigned t = t0; t <= t1; ++t) {
				point    const pts[2]     = {points[s][t], points[sn][t]};
				vector3d const normals[2] = {norms [s][t], norms [sn][t]};
				bool draw(disable_bfc);

				for (unsigned d = 0; d < 2 && !draw; ++d) {
					float const dp(dot_product_ptv(normals[d], vfrom, pts[d]));
					
					if (dp >= 0.0) {
						draw = 1;
					}
					else if (perturb_map != NULL) { // sort of a hack, not always correct but good enough in most cases
						float const dist_sq(p2p_dist_sq(pos, pts[d]));
						if (dist_sq > toler && (dist_sq > dmax_sq || dp > -0.3*p2p_dist(vfrom, pts[d]))) draw = 1;
					}
				}
				if (!draw) {continue;}

				for (unsigned i = 0; i < 2; ++i) {
					if (texture) {
						vntc.push_back(vertex_type_t(pts[i], normals[i], tscale*(1.0f - (i ? snt : s)*ndiv_inv), tscale*(1.0f - t*ndiv_inv)));
					}
					else {
						vn.push_back(vert_norm(pts[i], normals[i]));
					}
				}
			} // for t
		}
	} // for s
	if (use_quads) {
		if (texture) {draw_quad_verts_as_tris_and_clear(vntc);} else {draw_quad_verts_as_tris_and_clear(vn);}
	}
	else {
		if (texture) {draw_and_clear_verts(vntc, GL_TRIANGLE_STRIP);} else {draw_and_clear_verts(vn, GL_TRIANGLE_STRIP);}
	}
}


void sd_sphere_d::get_quad_points(vector<vert_norm_tc> &quad_pts) const { // used for scenery, not using vertex_type_t here

	assert(spn.ndiv > 0);
	float const ndiv_inv(1.0/float(spn.ndiv));
	point **points   = spn.points;
	vector3d **norms = spn.norms;
	if (quad_pts.empty()) {quad_pts.reserve(4*spn.ndiv*spn.ndiv);}
	
	for (unsigned s = 0; s < spn.ndiv; ++s) {
		unsigned const sn((s+1)%spn.ndiv), snt(min((s+1), spn.ndiv));

		for (unsigned t = 0; t < spn.ndiv; ++t) {
			point          pts[4]     = {points[s][t], points[sn][t], points[sn][t+1], points[s][t+1]};
			vector3d const normals[4] = {norms [s][t], norms [sn][t], norms [sn][t+1], norms [s][t+1]};

			for (unsigned i = 0; i < 4; ++i) {
				quad_pts.push_back(vert_norm_tc(pts[i], normals[i], (1.0f - (((i&1)^(i>>1)) ? snt : s)*ndiv_inv), (1.0f - ((i>>1) ? t+1 : t)*ndiv_inv)));
			}
		}
	}
}


void sd_sphere_d::get_triangles(vector<vert_wrap_t> &verts) const {

	assert(spn.ndiv > 0);
	point **points = spn.points;
	
	for (unsigned s = 0; s < spn.ndiv; ++s) {
		unsigned const sn((s+1)%spn.ndiv);

		for (unsigned t = 0; t < spn.ndiv; ++t) {
			verts.push_back(points[s ][t  ]); // 0
			verts.push_back(points[sn][t  ]); // 1
			verts.push_back(points[sn][t+1]); // 2
			verts.push_back(points[s ][t  ]); // 0
			verts.push_back(points[sn][t+1]); // 2
			verts.push_back(points[s ][t+1]); // 3
		}
	}
}


// Note: returns quads as triangles
void sd_sphere_d::get_triangles(vector<vertex_type_t> &verts, float s_beg, float s_end, float t_beg, float t_end) const {

	unsigned const ndiv(spn.ndiv);
	assert(ndiv > 0);
	float const ndiv_inv(1.0/float(ndiv));
	point **points   = spn.points;
	vector3d **norms = spn.norms;
	unsigned const s0(NDIV_SCALE(s_beg)), s1(NDIV_SCALE(s_end)), t0(NDIV_SCALE(t_beg)), t1(NDIV_SCALE(t_end));
	
	for (unsigned s = s0; s < s1; ++s) {
		unsigned const sn((s+1)%ndiv), snt(s+1);

		for (unsigned t = t0; t < t1; ++t) {
			unsigned const ix(s*(ndiv+1)+t), tn(t+1);
			point          pts[4]     = {points[s][t], points[sn][t], points[sn][tn], points[s][tn]};
			vector3d const normals[4] = {norms [s][t], norms [sn][t], norms [sn][tn], norms [s][tn]};
			
			for (unsigned i = 0; i < 4; ++i) {
				verts.push_back(vertex_type_t(pts[i], normals[i],(1.0f - (((i&1)^(i>>1)) ? snt : s)*ndiv_inv), (1.0f - ((i>>1) ? tn : t)*ndiv_inv) ));
			}
		} // for t
	} // for s
}


void sd_sphere_d::get_triangle_strip_pow2(vector<vertex_type_t> &verts, unsigned skip) const {

	float const ndiv_inv(1.0/float(spn.ndiv));

	for (unsigned s = 0; s < spn.ndiv; s += skip) {
		s = min(s, spn.ndiv-1);
		unsigned const sn((s+skip)%spn.ndiv), snt(min((s+skip), spn.ndiv));

		if (s != 0) { // add degenerate triangle to preserve the triangle strip
			for (unsigned d = 0; d < 2; ++d) {verts.push_back(vertex_type_t(spn.points[s][0], zero_vector, 0, 0));}
		}
		for (unsigned t = 0; t <= spn.ndiv; t += skip) {
			t = min(t, spn.ndiv);
			verts.push_back(vertex_type_t(spn.points[s ][t], spn.norms[s ][t], (1.0f - s  *ndiv_inv), (1.0f - t*ndiv_inv)));
			verts.push_back(vertex_type_t(spn.points[sn][t], spn.norms[sn][t], (1.0f - snt*ndiv_inv), (1.0f - t*ndiv_inv)));
		}
	} // for s
}


void sd_sphere_d::get_triangle_vertex_list(vector<vertex_type_t> &verts) const {

	float const ndiv_inv(1.0/float(spn.ndiv));

	for (unsigned s = 0; s <= spn.ndiv; ++s) {
		unsigned const six(s%spn.ndiv);

		for (unsigned t = 0; t <= spn.ndiv; ++t) {
			verts.push_back(vertex_type_t(spn.points[six][t], spn.norms[six][t], (1.0f - s*ndiv_inv), (1.0f - t*ndiv_inv)));
		}
	}
	assert(verts.size() < (1ULL << 8*sizeof(index_type_t)));
}


void sd_sphere_d::get_triangle_index_list_pow2(vector<index_type_t> &indices, unsigned skip) const {

	unsigned const stride(spn.ndiv + 1);

	for (unsigned s = 0; s < spn.ndiv; s += skip) {
		if (s != 0) { // add degenerate triangle to preserve the triangle strip
			for (unsigned d = 0; d < 2; ++d) {indices.push_back(s*stride);}
		}
		for (unsigned t = 0; t <= spn.ndiv; t += skip) {
			t = min(t, spn.ndiv);
			indices.push_back((s+0)   *stride + t);
			indices.push_back((s+skip)*stride + t);
		}
	}
}


void sd_sphere_vbo_d::ensure_vbos() {

	if (!vbo) {
		vector<vertex_type_t> verts;
		get_triangle_vertex_list(verts);
		create_vbo_and_upload(vbo, verts, 0, 1);
	}
	if (!ivbo) {
		assert(ix_offsets.empty());
		vector<index_type_t> indices;
		ix_offsets.push_back(0);
		
		for (unsigned n = spn.ndiv, skip = 1, ix_ix = 0; n >= 4; n >>= 1, skip <<= 1, ++ix_ix) {
			get_triangle_index_list_pow2(indices, skip);
			ix_offsets.push_back(indices.size());
		}
		create_vbo_and_upload(ivbo, indices, 1, 1);
	}
	assert(!ix_offsets.empty());
}


void sd_sphere_vbo_d::clear_vbos() {

	indexed_vbo_manager_t::clear_vbos();
	ix_offsets.clear();
}


unsigned calc_lod_pow2(unsigned max_ndiv, unsigned ndiv) {

	ndiv = max(ndiv, 4U);
	unsigned lod(0);
	for (unsigned n = (max_ndiv >> 1); ndiv <= n; n >>= 1, ++lod) {}
	return lod;
}


void sd_sphere_vbo_d::draw_ndiv_pow2(unsigned ndiv) {

	unsigned const lod(calc_lod_pow2(spn.ndiv, ndiv));
	static vector<vertex_type_t> verts;
	verts.resize(0);
	get_triangle_strip_pow2(verts, (1 << lod));
	draw_verts(verts, GL_TRIANGLE_STRIP);
}


void sd_sphere_vbo_d::draw_ndiv_pow2_vbo(unsigned ndiv) {

	unsigned const lod(calc_lod_pow2(spn.ndiv, ndiv));
	ensure_vbos();
	assert(lod+1 < ix_offsets.size());
	pre_render();
	vertex_type_t::set_vbo_arrays();
	glDrawRangeElements(GL_TRIANGLE_STRIP, 0, (spn.ndiv+1)*(spn.ndiv+1), (ix_offsets[lod+1] - ix_offsets[lod]),
		((sizeof(index_type_t) == 4) ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT),
		(void *)(ix_offsets[lod]*sizeof(index_type_t)));
	post_render();
}


void sd_sphere_vbo_d::draw_instances(unsigned ndiv, instance_render_t &inst_render) {

	unsigned const lod(calc_lod_pow2(spn.ndiv, ndiv));
	ensure_vbos();
	assert(lod+1 < ix_offsets.size());
	pre_render();
	vertex_type_t::set_vbo_arrays();
	inst_render.draw_and_clear(GL_TRIANGLE_STRIP, (ix_offsets[lod+1] - ix_offsets[lod]), vbo,
		((sizeof(index_type_t) == 4) ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT),
		(void *)(ix_offsets[lod]*sizeof(index_type_t)));
	post_render();
}


void get_sphere_triangles(vector<vert_wrap_t> &verts, point const &pos, float radius, int ndiv) {

	sd_sphere_d sd(pos, radius, ndiv);
	sd.gen_points_norms_static();
	sd.get_triangles(verts);
}


// non-collision object version
void draw_subdiv_sphere(point const &pos, float radius, int ndiv, point const &vfrom, float const *perturb_map,
						int texture, bool disable_bfc, bool const *const render_map, float const *const exp_map,
						point const *const pt_shift, float expand, float s_beg, float s_end, float t_beg, float t_end)
{
	sd_sphere_d sd(pos, radius, ndiv, perturb_map);
	sd.gen_points_norms_static(s_beg, s_end, t_beg, t_end);
	sd.draw_subdiv_sphere(vfrom, texture, disable_bfc, render_map, exp_map, pt_shift, expand, s_beg, s_end, t_beg, t_end);
}


void draw_subdiv_sphere(point const &pos, float radius, int ndiv, int texture, bool disable_bfc) {

	draw_subdiv_sphere(pos, radius, ndiv, (disable_bfc ? all_zeros : get_camera_all()), NULL, texture, disable_bfc);
}


void draw_subdiv_sphere_section(point const &pos, float radius, int ndiv, int texture,
								float s_beg, float s_end, float t_beg, float t_end)
{
	draw_subdiv_sphere(pos, radius, ndiv, all_zeros, NULL, texture, 1, NULL, NULL, NULL, 0.0, s_beg, s_end, t_beg, t_end);
}


void rotate_sphere_tex_to_dir(vector3d const &dir) { // dir must be normalized

	glRotatef(atan2(-dir.y, -dir.x)*TO_DEG-90.0, 0.0, 0.0, 1.0); // negate because dir was backwards
	glRotatef(asinf(-dir.z)*TO_DEG, 1.0, 0.0, 0.0);
}


void draw_single_colored_sphere(point const &pos, float radius, int ndiv, colorRGBA const &color) {

	shader_t s;
	s.begin_color_only_shader(color);
	draw_subdiv_sphere(pos, radius, ndiv, 0, 0);
	s.end_shader();
}


// unused
// less efficient than regular sphere
// textures must tile along all edges for this to look correct
void draw_cube_map_sphere(point const &pos, float radius, int ndiv, bool disable_bfc) {

	point pt;
	float const step(1.0/ndiv);
	point const camera(get_camera_all());
	vector<vert_norm_tc> verts;

	for (unsigned i = 0; i < 3; ++i) { // iterate over dimensions
		unsigned const d[2] = {i, ((i+1)%3)}, n((i+2)%3);

		for (unsigned j = 0; j < 2; ++j) { // iterate over opposing sides, min then max
			pt[n] = (float)j - 0.5;

			for (int s = 0; s < ndiv; ++s) {
				float const va[2] = {(step*(s+1)-0.5), (step*s-0.5)};

				for (int t = 0; t <= ndiv; ++t) {
					float tc[2];
					pt[d[1]] = tc[1] = step*t - 0.5;

					if (!disable_bfc) {
						bool back_facing(1);

						for (unsigned k = 0; k < 2 && back_facing; ++k) {
							point pt2(pt);
							pt2[d[0]] = va[k];
							if (dot_product_ptv(pt2, camera, (pt2.get_norm()*radius + pos)) > 0.0) back_facing = 0;
						}
						if (back_facing) continue;
					}
					for (unsigned k = 0; k < 2; ++k) { // iterate over vertices
						pt[d[0]] = tc[0] = va[k^j]; // need to orient the vertices differently for each side
						vector3d const norm(pt.get_norm());
						verts.push_back(vert_norm_tc((norm*radius + pos), norm, tc));
					}
				}
				draw_and_clear_verts(verts, GL_TRIANGLE_STRIP);
			} // for s
		} // for j
	} // for i
}


// ******************** TORUS ********************


void draw_torus(float ri, float ro, unsigned ndivi, unsigned ndivo, float tex_scale_i, float tex_scale_o) { // at (0,0,0) in z-plane, always textured

	assert(ndivi > 2 && ndivo > 2);
	float const ts(tex_scale_o/ndivo), tt(tex_scale_i/ndivi), ds(TWO_PI/ndivo), dt(TWO_PI/ndivi), cds(cos(ds)), sds(sin(ds));
	vector<float> sin_cos(2*ndivi);
	vector<vert_norm_tc> verts(2*(ndivi+1));

	for (unsigned t = 0; t < ndivi; ++t) {
		float const phi(t*dt);
		sin_cos[(t<<1)+0] = cos(phi);
		sin_cos[(t<<1)+1] = sin(phi);
	}
	for (unsigned s = 0; s < ndivo; ++s) { // outer
		float const theta(s*ds), ct(cos(theta)), st(sin(theta));
		point const pos[2] = {point(ct, st, 0.0), point((ct*cds - st*sds), (st*cds + ct*sds), 0.0)};

		for (unsigned t = 0; t <= ndivi; ++t) { // inner
			unsigned const t_((t == ndivi) ? 0 : t);
			float const cp(sin_cos[(t_<<1)+0]), sp(sin_cos[(t_<<1)+1]);

			for (unsigned i = 0; i < 2; ++i) {
				vector3d const delta(point(0.0, 0.0, cp) + pos[1-i]*sp);
				verts[(t<<1)+i] = vert_norm_tc((pos[1-i]*ro + delta*ri), delta, ts*(s+1-i), tt*t);
			}
		}
		draw_verts(verts, GL_TRIANGLE_STRIP);
	}
}


// ******************** QUAD/CUBE/POLYGON ********************


void rotate_towards_camera(point const &pos) {

	rotate_into_plus_z((get_camera_pos() - pos));
}


void enable_flares(colorRGBA const &color, bool zoomed) { // used for clouds and smoke

	color.do_glColor();
	glDepthMask(GL_FALSE); // not quite right - prevents flares from interfering with each other but causes later shapes to be drawn on top of the flares
	enable_blend();
	if (draw_model == 0) {select_texture(zoomed ? BLUR_CENT_TEX : BLUR_TEX);}
}

void disable_flares() {

	disable_blend();
	glDepthMask(GL_TRUE);
}


void draw_one_tquad(float x1, float y1, float x2, float y2, float z) { // Note: normal is +z

	vert_norm_tc verts[4];
	verts[0] = vert_norm_tc(point(x1, y1, z), plus_z, 0, 0);
	verts[1] = vert_norm_tc(point(x1, y2, z), plus_z, 0, 1);
	verts[2] = vert_norm_tc(point(x2, y2, z), plus_z, 1, 1);
	verts[3] = vert_norm_tc(point(x2, y1, z), plus_z, 1, 0);
	draw_verts(verts, 4, GL_TRIANGLE_FAN);
}

void draw_tquad(float xsize, float ysize, float z) { // Note: normal is +z
	draw_one_tquad(-xsize, -ysize, xsize, ysize, z);
}


// ordered p1+, p1-, p2-, p2+
bool get_line_as_quad_pts(point const &p1, point const &p2, float w1, float w2, point pts[4]) {

	int npts(0);
	vector3d const v1(get_camera_pos(), (p1 + p2)*0.5);
	vector3d v2;
	float const v1_mag(v1.mag());
	if (v1_mag > 1.0E5*min(w1, w2)) return 0; // too far away
	cylinder_quad_projection(pts, cylinder_3dw(p1, p2, w1, w2), v1/v1_mag, v2, npts);
	assert(npts == 4);
	return 1;
}


void line_tquad_draw_t::add_line_as_tris(point const &p1, point const &p2, float w1, float w2, colorRGBA const &color1, colorRGBA const &color2,
	point const* const prev, point const *const next, bool make_global)
{
	assert(w1 > 0.0 && w2 > 0.0 && color1.is_valid() && color2.is_valid()); // validate
	point pts[5], pts2[4];
	if (!get_line_as_quad_pts(p1, p2, w1, w2, pts)) return;

	if (prev && *prev != p1 && get_line_as_quad_pts(*prev, p1, w1, w1, pts2)) {
		pts[0] = 0.5*(pts[0] + pts2[3]); // average the points
		pts[1] = 0.5*(pts[1] + pts2[2]); // average the points
	}
	if (next && *next != p2 && get_line_as_quad_pts(p2, *next, w2, w2, pts2)) {
		pts[2] = 0.5*(pts[2] + pts2[1]); // average the points
		pts[3] = 0.5*(pts[3] + pts2[0]); // average the points
	}
	pts[4] = p2;
	int const ptix[9] = {2, 1, 4, 4, 1, 0, 4, 0, 3};
	float const tc[9] = {0.0, 0.0, 0.5, 0.5, 0.0, 1.0, 0.5, 1.0, 1.0};
	colorRGBA const color[9] = {color2, color1, color2, color2, color1, color1, color2, color1, color2};

	for (unsigned i = 0; i < 9; ++i) { // draw as 3 triangles
		point const pt(make_global ? make_pt_global(pts[ptix[i]]) : pts[ptix[i]]);
		verts.push_back(vert_tc_color(pt, tc[i], 0.5, color[i])); // tc for 1D blur texture
	}
}


void line_tquad_draw_t::draw() const { // supports quads and triangles

	shader_t s;
	s.begin_simple_textured_shader(0.01);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	enable_blend();
	select_texture(BLUR_TEX);
	draw_verts(verts, GL_TRIANGLES);
	disable_blend();
	s.end_shader();
}


void pos_dir_up::draw_frustum() const {

	float const nf_val[2] = {near_, far_};
	point pts[2][4]; // {near, far} x {ll, lr, ur, ul}
	vector<vert_wrap_t> verts;

	for (unsigned d = 0; d < 2; ++d) {
		point const center(pos + dir*nf_val[d]); // plane center
		float const dy(nf_val[d]*tterm/sqrt(1.0 + A*A)), dx(A*dy); // d*sin(theta)
		pts[d][0] = center - cp*dx - upv_*dy;
		pts[d][1] = center + cp*dx - upv_*dy;
		pts[d][2] = center + cp*dx + upv_*dy;
		pts[d][3] = center - cp*dx + upv_*dy;
		for (unsigned i = 0; i < 4; ++i) {verts.push_back(pts[d][i]);} // near and far clipping planes
	}
	for (unsigned i = 0; i < 4; ++i) { // sides
		verts.push_back(pts[0][(i+0)&3]);
		verts.push_back(pts[0][(i+1)&3]);
		verts.push_back(pts[1][(i+1)&3]);
		verts.push_back(pts[1][(i+0)&3]);
	}
	draw_quad_verts_as_tris(verts);
}


void draw_simple_cube(cube_t const &c, bool texture) {

	draw_cube(c.get_cube_center(), (c.d[0][1]-c.d[0][0]), (c.d[1][1]-c.d[1][0]), (c.d[2][1]-c.d[2][0]), texture);
}


// need to do something with tex coords for scale
void draw_cube(point const &pos, float sx, float sy, float sz, bool texture, bool scale_ndiv,
			   float texture_scale, bool proportional_texture, vector3d const *const view_dir)
{
	point const scale(sx, sy, sz);
	point const xlate(pos - 0.5*scale); // move origin from center to min corner
	float const sizes[3] = {sx, sy, sz};
	vert_norm_tc verts[24];
	unsigned vix(0);
		
	for (unsigned i = 0; i < 3; ++i) { // iterate over dimensions
		unsigned const d[2] = {i, ((i+1)%3)}, n((i+2)%3);

		for (unsigned j = 0; j < 2; ++j) { // iterate over opposing sides, min then max
			if (view_dir && (((*view_dir)[n] < 0.0) ^ j)) continue; // back facing
			vector3d norm(zero_vector);
			point pt;
			norm[n] = (2.0*j - 1.0); // -1 or 1
			pt[n]   = j;

			for (unsigned s1 = 0; s1 < 2; ++s1) {
				pt[d[1]] = s1;

				for (unsigned k = 0; k < 2; ++k, ++vix) { // iterate over vertices
					pt[d[0]] = k^j^s1^1; // need to orient the vertices differently for each side
					verts[vix].v = pt*scale + xlate;
					verts[vix].n = norm;

					if (texture) {
						verts[vix].t[0] = (proportional_texture ? sizes[d[1]] : 1.0)*texture_scale*pt[d[1]];
						verts[vix].t[1] = (proportional_texture ? sizes[d[0]] : 1.0)*texture_scale*pt[d[0]];
					}
				}
			}
		} // for j
	} // for i
	assert(vix <= 24);
	draw_quad_verts_as_tris(verts, vix);
}



void gen_quad_tex_coords(float *tdata, unsigned num, unsigned stride) { // stride is in floats

	for (unsigned i = 0, off = 0; i < num; i++) { // 00 10 11 01 for every quad
		tdata[off] = 0.0; tdata[off+1] = 0.0; off += stride;
		tdata[off] = 1.0; tdata[off+1] = 0.0; off += stride;
		tdata[off] = 1.0; tdata[off+1] = 1.0; off += stride;
		tdata[off] = 0.0; tdata[off+1] = 1.0; off += stride;
	}
}


void gen_quad_tri_tex_coords(float *tdata, unsigned num, unsigned stride) { // stride is in floats

	for (unsigned i = 0, off = 0; i < num; i++) { // for every tri
		tdata[off] = -0.5; tdata[off+1] =  1.0; off += stride;
		tdata[off] =  0.5; tdata[off+1] = -1.0; off += stride;
		tdata[off] =  1.5; tdata[off+1] =  1.0; off += stride;
	}
}


// ******************** Sphere VBOs ********************


void free_sphere_vbos() {

	delete_and_zero_vbo(predef_sphere_vbo);
}


void setup_sphere_vbos() {

	assert(N_SPHERE_DIV > 0);
	if (predef_sphere_vbo > 0) return; // already finished
	vector<sd_sphere_d::vertex_type_t> verts;
	sphere_point_norm spn;
	sphere_vbo_offsets[0] = 0;

	for (unsigned i = 1; i <= N_SPHERE_DIV; ++i) {
		for (unsigned half = 0; half < 2; ++half) {
			for (unsigned tex = 0; tex < 2; ++tex) {
				sd_sphere_d sd(all_zeros, 1.0, i);
				sd.gen_points_norms(spn, 0.0, 1.0, 0.0, (half ? 0.5 : 1.0));
				sd.get_triangles(verts,  0.0, 1.0, 0.0, (half ? 0.5 : 1.0));
				sphere_vbo_offsets[((i-1) << 2) + (half << 1) + tex] = verts.size();
			}
		}
	}
	create_vbo_and_upload(predef_sphere_vbo, verts, 0, 1);
}


void draw_sphere_vbo_raw(int ndiv, bool textured, bool half) {

	assert(ndiv > 0 && ndiv <= N_SPHERE_DIV);
	assert(predef_sphere_vbo > 0);
	unsigned const ix(((ndiv-1) << 2) + (half << 1) + textured), off1(sphere_vbo_offsets[ix-1]), off2(sphere_vbo_offsets[ix]);
	assert(off1 < off2);
	bind_vbo(predef_sphere_vbo);
	set_array_client_state(1, textured, 1, 0);
	sd_sphere_d::vertex_type_t::set_vbo_arrays(0, 0);
	glDrawArrays(GL_QUADS, off1, (off2 - off1)); // FIXME: use index arrays?
	bind_vbo(0);
}


void draw_sphere_vbo(point const &pos, float radius, int ndiv, bool textured, bool half, bool bfc, int shader_loc) {

	if (USE_SPHERE_VBOS && ndiv <= N_SPHERE_DIV) { // speedup is highly variable
		assert(ndiv > 0);
		bool const has_xform(radius != 1.0 || pos != all_zeros);

		if (has_xform) {
			if (shader_loc >= 0) { // unused/untested mode
				vector4d const v(pos, radius);
				shader_t::set_uniform_vector4d(shader_loc, v);
			}
			else {
				glPushMatrix();
				if (pos != all_zeros) translate_to(pos);
				if (radius != 1.0)    uniform_scale(radius);
			}
		}
		draw_sphere_vbo_raw(ndiv, textured, half);
		
		if (has_xform) {
			if (shader_loc >= 0) {
				shader_t::set_uniform_color(shader_loc, BLACK); // pos=0,0,0, scale=1.0
			}
			else {
				glPopMatrix();
			}
		}
	}
	else if (half) {
		draw_subdiv_sphere_section(pos, radius, ndiv, textured, 0.0, 1.0, 0.0, 0.5);
	}
	else {
		draw_subdiv_sphere(pos, radius, ndiv, textured, !bfc);
	}
}


void draw_sphere_vbo_back_to_front(point const &pos, float radius, int ndiv, bool textured) {

	glEnable(GL_CULL_FACE);

	for (unsigned i = 0; i < 2; ++i) { // kind of slow
		glCullFace(i ? GL_BACK : GL_FRONT);
		draw_sphere_vbo(pos, radius, ndiv, textured); // cull?, partial sphere?
	}
	glDisable(GL_CULL_FACE);
}

