// 3D World - OpenGL CS184 Computer Graphics Project
// by Frank Gennari
// 5/1/02

#include "3DWorld.h"
#include "mesh.h"
#include "transform_obj.h"
#include "player_state.h"
#include "physics_objects.h"


bool const REMOVE_ALL_COLL   = 1;
bool const ALWAYS_ADD_TO_HCM = 0;
bool const USE_COLL_BORDER   = 1;
unsigned const CAMERA_STEPS  = 10;
unsigned const PURGE_THRESH  = 20;
unsigned const CVZ_NDIV      = 32;
float const CAMERA_MESH_DZ   = 0.1; // max dz on mesh


// Global Variables
bool have_drawn_cobj, have_platform_cobj, camera_on_snow(0);
int coll_border(0), camera_coll_id(-1);
unsigned cobjs_removed(0), index_top(0);
float czmin(FAR_CLIP), czmax(-FAR_CLIP), coll_rmax(0.0);
point camera_last_pos(all_zeros); // not sure about this, need to reset sometimes
vector<int> index_stack;
vector<coll_obj> coll_objects;
extern platform_cont platforms;

extern int camera_coll_smooth, game_mode, world_mode, xoff, yoff, camera_change, display_mode, scrolling, animate2;
extern int camera_in_air, invalid_collision, mesh_scale_change, camera_invincible, flight, do_run, cobj_counter;
extern float TIMESTEP, temperature, zmin, base_gravity, ftick, tstep, zbottom, ztop, fticks, max_obj_radius;
extern double camera_zh;
extern dwobject def_objects[];
extern obj_type object_types[];
extern player_state *sstates;


void add_coll_point(int i, int j, int index, float zminv, float zmaxv, int add_to_hcm, int is_dynamic, int dhcm);
int  get_next_avail_index();
void free_index(int index);
void set_coll_obj_props(int index, int type, float radius, float radius2, int platform_id, cobj_params const &cparams);

bool get_snow_height(point const &p, float radius, float &zval, vector3d &norm, bool crush_snow);



inline void get_params(int &x1, int &y1, int &x2, int &y2, int &cb, const float d[3][2], int dhcm, int min_cb=0) {

	float const rmax(USE_COLL_BORDER ? 0.0 : max_obj_radius);
	cb = max(min_cb, ((dhcm == 2) ? 0 : coll_border));
	x1 = max(cb, (get_xpos(d[0][0] - rmax)));
	y1 = max(cb, (get_ypos(d[1][0] - rmax)));
	x2 = min((MESH_X_SIZE-cb-1), (get_xpos(d[0][1] + rmax)));
	y2 = min((MESH_Y_SIZE-cb-1), (get_ypos(d[1][1] + rmax)));
}


void set_coll_border() {

	coll_border = (USE_COLL_BORDER ? int((max_obj_radius/max(DX_VAL, DY_VAL)) + 1) : 0); // + 0.5?
}


void add_coll_cube_to_matrix(int index, int dhcm) {

	int x1, x2, y1, y2, cb;
	coll_obj &cobj(coll_objects[index]);
	bool const is_dynamic(cobj.status == COLL_DYNAMIC);
	float ds[3][2];
	vector3d delta(zero_vector);

	// we adjust the size of the cube to account for all possible platform locations
	// if delta is not aligned with x/y/z axes then the boundary will be an over approximation, which is inefficient but ok
	if (cobj.platform_id >= 0) {
		assert(cobj.platform_id < (int)platforms.size());
		delta = platforms[cobj.platform_id].get_range();
	}
	for (unsigned j = 0; j < 3; ++j) {
		ds[j][0] = cobj.d[j][0] + min(delta[j], 0.0f);
		ds[j][1] = cobj.d[j][1] + max(delta[j], 0.0f);
	}
	get_params(x1, y1, x2, y2, cb, ds, dhcm);

	for (int i = y1-cb; i <= y2+cb; ++i) {
		for (int j = x1-cb; j <= x2+cb; ++j) {
			int add_to_hcm(i >= y1-1 && i <= y2+1 && j >= x1-1 && j <= x2+1);
			add_coll_point(i, j, index, ds[2][0], ds[2][1], add_to_hcm, is_dynamic, dhcm);
		}
	}
}


int add_coll_cube(cube_t &cube, cobj_params const &cparams, int platform_id, int dhcm) {

	int const index(get_next_avail_index());
	coll_obj &cobj(coll_objects[index]);
	cube.normalize();
	cobj.copy_from(cube);
	// cache the center point and radius
	cobj.points[0] = cobj.get_center();
	set_coll_obj_props(index, COLL_CUBE, cobj.get_bsphere_radius(), 0.0, platform_id, cparams);
	add_coll_cube_to_matrix(index, dhcm);
	return index;
}


void add_coll_cube_hollow(int *index, float d[3][2], cobj_params const &cparams, int platform_id, float thickness) { // add six coll polygons

	point points[4];
	assert(index != NULL);

	for (unsigned i = 0; i < 4; ++i) {
		points[i].assign(d[0][i==1||i==2], d[1][i==2||i==3], d[2][1]);
	}
	index[0] = add_coll_polygon(points, 4, cparams, platform_id, thickness);
	points[0].z = points[1].z = points[2].z = points[3].z = d[2][0];
	index[1] = add_coll_polygon(points, 4, cparams, platform_id, thickness);
	
	for (unsigned i = 0; i < 4; ++i) {
		points[i].assign(d[0][i==2||i==3], d[1][1], d[2][i==1||i==2]);
	}
	index[2] = add_coll_polygon(points, 4, cparams, platform_id, thickness);
	points[0].y = points[1].y = points[2].y = points[3].y = d[1][0];
	index[3] = add_coll_polygon(points, 4, cparams, platform_id, thickness);
	
	for (unsigned i = 0; i < 4; ++i) {
		points[i].assign(d[0][1], d[1][i==1||i==2], d[2][i==2||i==3]);
	}
	index[4] = add_coll_polygon(points, 4, cparams, platform_id, thickness);
	points[0].x = points[1].x = points[2].x = points[3].x = d[0][0];
	index[5] = add_coll_polygon(points, 4, cparams, platform_id, thickness);
}


void add_coll_cylinder_to_matrix(int index, int dhcm) {

	int xx1, xx2, yy1, yy2, cb;
	coll_obj &cobj(coll_objects[index]);
	float zminc(cobj.d[2][0]), zmaxc(cobj.d[2][1]), zmin0(zminc), zmax0(zmaxc);
	point const p1(cobj.points[0]), p2(cobj.points[1]);
	float const x1(p1.x), x2(p2.x), y1(p1.y), y2(p2.y), z1(p1.z), z2(p2.z);
	float const radius(cobj.radius), radius2(cobj.radius2), dr(radius2 - radius), rscale((z1-z2)/fabs(dr));
	float const length(p2p_dist(p1, p2)), dt(HALF_DXY/length), r_off(radius + dt*fabs(dr));
	int const radx(int(ceil(radius*DX_VAL_INV))+1), rady(int(ceil(radius*DY_VAL_INV))+1), rxry(radx*rady);
	int const xpos(get_xpos(x1)), ypos(get_ypos(y1));
	bool const is_dynamic(cobj.status == COLL_DYNAMIC);
	get_params(xx1, yy1, xx2, yy2, cb, cobj.d, dhcm);

	if (cobj.type == COLL_CYLINDER_ROT) {
		float xylen(sqrt((x2-x1)*(x2-x1) + (y2-y1)*(y2-y1)));
		float const rmin(min(radius, radius2)), rmax(max(radius, radius2));
		bool const vertical(x1 == x2 && y1 == y2), horizontal(fabs(z1 - z2) < TOLERANCE);
		bool const vert_trunc_cone(z1 != z2 && radius != radius2 && rmax > HALF_DXY);

		for (int i = yy1-cb; i <= yy2+cb; ++i) {
			float const yv(get_yval(i)), v2y(y1 - yv);

			for (int j = xx1-cb; j <= xx2+cb; ++j) {
				float xv(get_xval(j));

				if (vertical) { // vertical
					if (vert_trunc_cone) { // calculate zmin/zmax
						xv -= x1;
						float const rval(min(rmax, (float)sqrt(xv*xv + v2y*v2y)));
						zmaxc = ((rval > rmin) ? max(zmin0, min(zmax0, (rscale*(rval - rmin) + z2))) : zmax0);
					} // else near constant radius, so zminc/zmaxc are correct
				}
				else if (horizontal) {
					zminc = z1 - rmax;
					zmaxc = z1 + rmax;
				}
				else { // diagonal
					// too complex/slow to get this right, so just use the bbox zminc/zmaxc
				}
				int add_to_hcm(0);
				
				if (i >= yy1-1 && i <= yy2+1 && j >= xx1-1 && j <= xx2+1) {
					if (vertical) {
						add_to_hcm = ((i-ypos)*(i-ypos) + (j-xpos)*(j-xpos) <= rxry);
					}
					else {
						float const dist(fabs((x2-x1)*(y1-yv) - (x1-xv)*(y2-y1))/xylen - HALF_DXY);

						if (dist < rmax) {
							if (horizontal) {
								float const t(((x1-x2)*(x1-xv) + (y1-y2)*(y1-yv))/(xylen*xylen)); // location along cylinder axis
								add_to_hcm = (t >= -dt && t <= 1.0+dt && dist < min(rmax, (r_off + t*dr)));
							}
							else { // diagonal
								add_to_hcm = 1; // again, too complex/slow to get this right, so just be conservative
							}
						}
					}
				}
				add_coll_point(i, j, index, zminc, zmaxc, add_to_hcm, is_dynamic, dhcm);
			}
		}
	}
	else { // standard vertical constant-radius cylinder
		assert(cobj.type == COLL_CYLINDER);
		int const crsq((radx + cb)*(rady + cb));

		for (int i = yy1-cb; i <= yy2+cb; ++i) {
			for (int j = xx1-cb; j <= xx2+cb; ++j) {
				int const distsq((i - ypos)*(i - ypos) + (j - xpos)*(j - xpos));
				if (distsq <= crsq) add_coll_point(i, j, index, z1, z2, (distsq <= rxry), is_dynamic, dhcm);
			}
		}
	}
}


int add_coll_cylinder(float x1, float y1, float z1, float x2, float y2, float z2, float radius, float radius2,
					  cobj_params const &cparams, int platform_id, int dhcm)
{
	int type;
	int const index(get_next_avail_index());
	coll_obj &cobj(coll_objects[index]);
	radius  = fabs(radius);
	radius2 = fabs(radius2);
	float const rav(max(radius, radius2));
	assert(radius > 0.0 || radius2 > 0.0);
	cobj.d[0][0] = min(x1, x2) - rav;
	cobj.d[0][1] = max(x1, x2) + rav;
	cobj.d[1][0] = min(y1, y2) - rav;
	cobj.d[1][1] = max(y1, y2) + rav;
	int const vertical(x1 == x2 && y1 == y2), nonvert(!vertical || (fabs(radius - radius2)/max(radius, radius2)) > 0.2);

	if (nonvert) {
		if (vertical) {
			cobj.d[2][0] = min(z1, z2);
			cobj.d[2][1] = max(z1, z2);
		}
		else {
			cobj.d[2][0] = min(z1-radius, z2-radius2);
			cobj.d[2][1] = max(z1+radius, z2+radius2);
		}
		type = COLL_CYLINDER_ROT;
	}
	else { // standard vertical constant-radius cylinder
		if (z2 < z1) swap(z2, z1);
		cobj.d[2][0] = z1;
		cobj.d[2][1] = z2;
		type         = COLL_CYLINDER;
	}
	point *points = cobj.points;
	points[0].x = x1; points[0].y = y1; points[0].z = z1;
	points[1].x = x2; points[1].y = y2; points[1].z = z2;
	
	if (dist_less_than(points[0], points[1], TOLERANCE)) { // no near zero length cylinders
		cout << "pt0 = "; points[0].print(); cout << ", pt1 = "; points[1].print(); cout << endl;
		assert(0);
	}
	set_coll_obj_props(index, type, radius, radius2, platform_id, cparams);
	add_coll_cylinder_to_matrix(index, dhcm);
	return index;
}


void add_coll_sphere_to_matrix(int index, int dhcm) {

	int x1, x2, y1, y2, cb;
	coll_obj &cobj(coll_objects[index]);
	point const pt(cobj.points[0]);
	bool const is_dynamic(cobj.status == COLL_DYNAMIC);
	float const radius(cobj.radius);
	int const radx(int(radius*DX_VAL_INV) + 1), rady(int(radius*DY_VAL_INV) + 1);
	int const xpos(get_xpos(pt.x)), ypos(get_ypos(pt.y));
	get_params(x1, y1, x2, y2, cb, cobj.d, dhcm);
	int const rxry(radx*rady), crsq((radx + cb)*(rady + cb));

	for (int i = y1-cb; i <= y2+cb; ++i) {
		for (int j = x1-cb; j <= x2+cb; ++j) {
			int const distsq((i - ypos)*(i - ypos) + (j - xpos)*(j - xpos));

			if (distsq <= crsq) { // nasty offset by HALF_DXY to account for discretization error
				float const dz(sqrt(max(0.0f, (radius*radius - max(0.0f, (distsq*dxdy - HALF_DXY))))));
				add_coll_point(i, j, index, (pt.z-dz), (pt.z+dz), (distsq <= rxry), is_dynamic, dhcm);
			}
		}
	}
}


// doesn't work for ellipses when X != Y
int add_coll_sphere(point const &pt, float radius, cobj_params const &cparams, int platform_id, int dhcm) {

	radius = fabs(radius);
	int const index(get_next_avail_index());
	coll_obj &cobj(coll_objects[index]);

	for (unsigned i = 0; i < 3; ++i) {
		cobj.d[i][0] = pt[i] - radius;
		cobj.d[i][1] = pt[i] + radius;
	}
	cobj.points[0] = pt;
	set_coll_obj_props(index, COLL_SPHERE, radius, radius, platform_id, cparams);
	add_coll_sphere_to_matrix(index, dhcm);
	return index;
}


void add_coll_polygon_to_matrix(int index, int dhcm) { // coll_obj member function?

	int x1, x2, y1, y2, cb;
	coll_obj &cobj(coll_objects[index]);
	get_params(x1, y1, x2, y2, cb, cobj.d, dhcm, 1); // ensure at least a cb of 1 for best tree leaf shadows
	bool const is_dynamic(cobj.status == COLL_DYNAMIC);
	vector3d const norm(cobj.norm);
	float const dval(-dot_product(norm, cobj.points[0]));
	float const zminc(cobj.d[2][0]), zmaxc(cobj.d[2][1]); // thickness has already been added/subtracted
	float const height(0.5*fabs(norm.z*cobj.thickness)), expand(DX_VAL + DY_VAL);
	float const thick(0.5*cobj.thickness), dx((cb+0.5)*DX_VAL), dy((cb+0.5)*DY_VAL);
	float const dzx(norm.z == 0.0 ? 0.0 : DX_VAL*norm.x/norm.z), dzy(norm.z == 0.0 ? 0.0 : DY_VAL*norm.y/norm.z);
	float const delta_z(sqrt(dzx*dzx + dzy*dzy));
	vector<vector<point> > const *pts(NULL);
	if (cobj.thickness > MIN_POLY_THICK2) pts = &thick_poly_to_sides(cobj.points, cobj.npoints, norm, cobj.thickness);
	cube_t cube;
	cube.d[2][0] = zminc - SMALL_NUMBER;
	cube.d[2][1] = zmaxc + SMALL_NUMBER;

	for (int i = y1-cb; i <= y2+cb; ++i) {
		float const yv(get_yval(i));
		cube.d[1][0] = yv - dy - (i<=y1)*thick;
		cube.d[1][1] = yv + dy + (i>=y2)*thick;

		for (int j = x1-cb; j <= x2+cb; ++j) {
			float const xv(get_xval(j));
			float z1(zminc), z2(zmaxc);
			bool const add_to_hcm(i >= y1-1 && i <= y2+1 && j >= x1-1 && j <= x2+1);
			cube.d[0][0] = xv - dx - (j<=x1)*thick;
			cube.d[0][1] = xv + dx + (j>=x2)*thick;

			if (add_to_hcm) {
				swap(z1, z2);

				if (cobj.thickness > MIN_POLY_THICK2) { // thick polygon	
					assert(pts);
					bool inside(0);

					for (unsigned k = 0; k < pts->size(); ++k) {
						point const *const p(&(*pts)[k].front());
						vector3d const pn(get_poly_norm(p));

						if (fabs(pn.z) > 1.0E-3) { // ignore near-vertical polygon edges (for now)
							inside |= get_poly_zminmax(p, (*pts)[k].size(), pn, -dot_product(pn, p[0]), cube, z1, z2);
						}
					}
					if (!inside) continue;
				}
				else if (!get_poly_zminmax(cobj.points, cobj.npoints, norm, dval, cube, z1, z2)) {
					continue;
				}
				// adjust z bounds so that they are for the entire cell x/y bounds, not a single point (conservative)
				z1 = max(zminc, (z1 - delta_z));
				z2 = min(zmaxc, (z2 + delta_z));
			}
			add_coll_point(i, j, index, z1, z2, add_to_hcm, is_dynamic, dhcm);
		}
	}
}


// must be planar, convex polygon with unique consecutive points
int add_coll_polygon(const point *points, int npoints, cobj_params const &cparams, float thickness, int platform_id, int dhcm) {

	assert(npoints >= 3 && points != NULL); // too strict?
	assert(npoints <= N_COLL_POLY_PTS);
	int const index(get_next_avail_index());
	coll_obj &cobj(coll_objects[index]);
	if (thickness == 0.0) thickness = MIN_POLY_THICK;
	cobj.norm = get_poly_norm(points);
	assert(cobj.norm != zero_vector);
	cobj.set_from_points(points, npoints); // set cube_t
	
	for (unsigned p = 0; p < 3; ++p) {
		float const thick(0.5*thickness*fabs(cobj.norm[p]));
		cobj.d[p][0] -= thick;
		cobj.d[p][1] += thick;
	}
	for (int i = 0; i < npoints; ++i) {
		cobj.points[i] = points[i];
	}
	cobj.npoints   = npoints; // must do this after set_coll_obj_props
	cobj.thickness = thickness;
	float brad;
	point center; // unused
	polygon_bounding_sphere(points, npoints, thickness, center, brad);
	set_coll_obj_props(index, COLL_POLYGON, brad, 0.0, platform_id, cparams);
	add_coll_polygon_to_matrix(index, dhcm);
	return index;
}


void coll_obj::add_as_fixed_cobj() {

	calc_size();
	fixed = 1;
	id    = add_coll_cobj();
}


int coll_obj::add_coll_cobj() {

	int cid(-1);
	cp.is_dynamic = (status == COLL_DYNAMIC);

	switch (type) {
	case COLL_CUBE:
		cid = add_coll_cube(*this, cp, platform_id);
		break;
	case COLL_SPHERE:
		cid = add_coll_sphere(points[0], radius, cp, platform_id);
		break;
	case COLL_CYLINDER:
	case COLL_CYLINDER_ROT:
		cid = add_coll_cylinder(points[0], points[1], radius, radius2, cp, platform_id);
		break;
	case COLL_POLYGON:
		cid = add_coll_polygon(points, npoints, cp, thickness, platform_id);
		break;
	default:
		assert(0);
	}
	assert(size_t(cid) < coll_objects.size());
	coll_objects[cid].destroy     = destroy;
	coll_objects[cid].fixed       = fixed;
	return cid;
}


void coll_obj::re_add_coll_cobj(int index, int remove_old, int dhcm) {

	if (!fixed) return;
	assert(index >= 0);
	assert(id == -1 || id == (int)index);
	if (remove_old) remove_coll_object(id, 0); // might already have been removed

	switch (type) {
	case COLL_CUBE:
		add_coll_cube_to_matrix(index, dhcm);
		break;
	case COLL_SPHERE:
		add_coll_sphere_to_matrix(index, dhcm);
		break;
	case COLL_CYLINDER:
	case COLL_CYLINDER_ROT:
		add_coll_cylinder_to_matrix(index, dhcm);
		break;
	case COLL_POLYGON:
		add_coll_polygon_to_matrix(index, dhcm);
		break;
	default:
		assert(0);
	}
	cp.is_dynamic = 0;
	status        = COLL_STATIC;
	counter       = 0;
	id            = index;
}


void coll_obj::get_cvz_counts(int *zz, float zmin, float zmax, int x, int y) const {

	if (status != COLL_STATIC) {
		zz[0] = zz[1] = 0;
		return;
	}
	float const dz_inv((CVZ_NDIV-1)/(zmax-zmin));
	float zv[2] = {d[2][0], d[2][1]};

	// clip size to actual polygon bounds within this cell (could do this with COLL_CYLINDER_ROT as well)
	if (type == COLL_POLYGON && thickness <= MIN_POLY_THICK && (zz[1] - zz[0]) > 2 && radius > HALF_DXY && norm.z != 0.0) {
		float const D(-dot_product(norm, points[0]));
		float zval[2];

		for (int yy = y; yy <= y+1; ++yy) {
			for (int xx = x; xx <= x+1; ++xx) {
				float const z((-norm.x*get_xval(xx) - norm.y*get_yval(yy) - D)/norm.z);
				zval[0] = ((xx == x && yy == y) ? z : min(zval[0], z));
				zval[1] = ((xx == x && yy == y) ? z : max(zval[1], z));
			}
		}
		zv[0] = max(zv[0], zval[0]);
		zv[1] = min(zv[1], zval[1]);
	}
	for (unsigned i = 0; i < 2; ++i) {
		zz[i] = max(0, min(int(CVZ_NDIV)-1, int((zv[i] - zmin)*dz_inv + 0.5)));
	}
}


void coll_cell::clear_cvz() {

	if (cvz.capacity() > INIT_CCELL_SIZE) cvz.clear(); else cvz.resize(0);
}


void coll_cell::clear(bool clear_vectors) {

	if (clear_vectors) {
		if (cvals.capacity() > INIT_CCELL_SIZE) cvals.clear(); else cvals.resize(0);
		clear_cvz();
	}
	zmin = occ_zmin =  FAR_CLIP;
	zmax = occ_zmax = -FAR_CLIP;
}


void coll_cell::optimize(int x, int y) {

	if (scrolling) return; // optimize only at the end of a scrolling event
	clear_cvz();
	unsigned ncvals(cvals.size());
	if (ncvals < CVZ_NDIV || zmax <= zmin) return;
	unsigned ncv(0);

	for (unsigned i = 0; i < ncvals; ++i) {
		unsigned const ix(cvals[i]);
		assert(size_t(ix) < coll_objects.size());
		if (coll_objects[ix].status == COLL_STATIC) ++ncv;
	}
	if (ncv < CVZ_NDIV) return;
	cvz.resize(CVZ_NDIV);
	unsigned cnt[CVZ_NDIV] = {0};
	int zz[2];

	for (unsigned i = 0; i < ncvals; ++i) { // determine counts
		coll_objects[cvals[i]].get_cvz_counts(zz, zmin, zmax, x, y);
		for (int z = zz[0]; z <= zz[1]; ++z) ++cnt[z];
	}
	for (unsigned i = 0; i < CVZ_NDIV; ++i) {
		cvz[i].reserve(cnt[i]);
	}
	for (unsigned i = 0; i < ncvals; ++i) { // create vector
		coll_objects[cvals[i]].get_cvz_counts(zz, zmin, zmax, x, y);

		for (int z = zz[0]; z <= zz[1]; ++z) {
			cvz[z].push_back(cvals[i]);
		}
	}
}


inline void coll_cell::update_zmm(float zmin_, float zmax_, coll_obj const &cobj) {
		
	assert(zmin_ <= zmax_);

	if (cobj.is_occluder()) {
		occ_zmin = min(zmin_, occ_zmin);
		occ_zmax = max(zmax_, occ_zmax);
	}
	zmin = min(zmin_, zmin);
	zmax = max(zmax_, zmax);
	assert(zmin <= occ_zmin);
	assert(zmax >= occ_zmax);
}


void cobj_optimize() { // currently used for both statistical reporting and optimization

	unsigned ncv(0), nonempty(0), ncobj(0);
	RESET_TIME;

	for (int y = 0; y < MESH_Y_SIZE; ++y) {
		for (int x = 0; x < MESH_X_SIZE; ++x) {
			coll_cell &vcm(v_collision_matrix[y][x]);
			vcm.optimize(x, y);
			ncv += vcm.cvals.size();
			if (!vcm.cvals.empty()) ++nonempty;
		}
	}
	unsigned const csize(coll_objects.size());

	for (unsigned i = 0; i < csize; ++i) {
		if (coll_objects[i].status == COLL_STATIC) ++ncobj;
	}
	if (ncobj > 0) {
		PRINT_TIME("Optimize");
		cout << "bins = " << XY_MULT_SIZE << ", ne = " << nonempty << ", cobjs = " << ncobj
			 << ", ent = " << ncv << ", per c = " << ncv/ncobj << ", per bin = " << ncv/XY_MULT_SIZE << endl;
	}
}


void add_coll_point(int i, int j, int index, float zminv, float zmaxv, int add_to_hcm, int is_dynamic, int dhcm) {

	assert(!point_outside_mesh(j, i));
	coll_cell &vcm(v_collision_matrix[i][j]);
	assert((unsigned)index < coll_objects.size());
	vcm.add_entry(index);
	coll_obj const &cobj(coll_objects[index]);
	unsigned const size(vcm.cvals.size());

	if (size > 1 && cobj.status == COLL_STATIC && coll_objects[vcm.cvals[size-2]].status == COLL_DYNAMIC) {
		std::rotate(vcm.cvals.begin(), vcm.cvals.begin()+size-1, vcm.cvals.end()); // rotate last point to first point???
	}
	if (is_dynamic) return;

	// update the z values if this cobj is part of a vertically moving platform
	// if it's a cube then it's handled in add_coll_cube_to_matrix()
	if (cobj.type != COLL_CUBE && cobj.platform_id >= 0) {
		assert(cobj.platform_id < (int)platforms.size());
		vector3d const range(platforms[cobj.platform_id].get_range());

		if (range.x == 0.0 && range.y == 0.0) { // vertical platform
			if (range.z > 0.0) {
				zmaxv += range.z; // travels up
			}
			else {
				zminv += range.z; // travels down
			}
		}
	}
	if (dhcm == 0 && add_to_hcm && h_collision_matrix[i][j] < zmaxv && (mesh_height[i][j] + 2.0*object_types[SMILEY].radius) > zminv) {
		h_collision_matrix[i][j] = zmaxv;
	}
	if (add_to_hcm || ALWAYS_ADD_TO_HCM) {
		vcm.update_zmm(zminv, zmaxv, cobj);
		vcm.update_opt(j, i);
		czmin = min(zminv, czmin);
		czmax = max(zmaxv, czmax);
	}
}


// this needs to be modified to support random addition and removal of collision objects
// right now it only really works if objects are added and removed in the correct order
int remove_coll_object(int index, bool reset_draw) {

	if (index < 0) return 0;
	assert((size_t)index < coll_objects.size());
	int const status(coll_objects[index].status);

	if (status == COLL_UNUSED) {
		assert(REMOVE_ALL_COLL);
		return 0;
	}
	if (status == COLL_FREED) return 0;
	if (reset_draw) coll_objects[index].cp.draw = 0;
	coll_objects[index].status = COLL_FREED;
	
	if (status == COLL_STATIC) {
		//free_index(index); // can't do this here - object's collision id needs to be held until purge
		++cobjs_removed;
		return 0;
	}
	int x1, y1, x2, y2, cb;
	get_params(x1, y1, x2, y2, cb, coll_objects[index].d, 0);

	for (int i = y1-cb; i <= y2+cb; ++i) {
		for (int j = x1-cb; j <= x2+cb; ++j) {
			vector<int> &cvals(v_collision_matrix[i][j].cvals); // what about cvz?
			
			for (unsigned k = 0; k < cvals.size() ; ++k) {
				if (cvals[k] == index) {
					cvals.erase(cvals.begin()+k); // can't change zmin or zmax (I think)
					break; // should only be in here once
				}
			}
		}
	}
	free_index(index);
	return 1;
}


int remove_reset_coll_obj(int &index) {

	int const retval(remove_coll_object(index));
	index = -1;
	return retval;
}


void purge_coll_freed(bool force) {

	if (!force && cobjs_removed < PURGE_THRESH) return;
	//RESET_TIME;

	for (int i = 0; i < MESH_Y_SIZE; ++i) {
		for (int j = 0; j < MESH_X_SIZE; ++j) {
			bool changed(0);
			coll_cell &vcm(v_collision_matrix[i][j]);
			unsigned const size(vcm.cvals.size());

			for (unsigned k = 0; k < size && !changed; ++k) {
				if (coll_objects[vcm.cvals[k]].freed_unused()) changed = 1;
			}
			// Note: don't actually have to recalculate zmin/zmax unless a removed object was on the top or bottom of the coll cell
			if (!changed) continue;
			vcm.zmin = vcm.occ_zmin = mesh_height[i][j];
			vcm.zmax = vcm.occ_zmax = zmin;
			vector<int>::const_iterator in(vcm.cvals.begin());
			vector<int>::iterator o(vcm.cvals.begin());

			for (; in != vcm.cvals.end(); ++in) {
				coll_obj &cobj(coll_objects[*in]);

				if (!cobj.freed_unused()) {
					if (cobj.status == COLL_STATIC) vcm.update_zmm(cobj.d[2][0], cobj.d[2][1], cobj);
					*o++ = *in;
				}
			}
			vcm.cvals.erase(o, vcm.cvals.end()); // excess capacity?
			h_collision_matrix[i][j] = vcm.zmax; // need to think about add_to_hcm...
			vcm.update_opt(j, i);
		}
	}
	unsigned const ncobjs(coll_objects.size());

	for (unsigned i = 0; i < ncobjs; ++i) {
		if (coll_objects[i].status == COLL_FREED) free_index(i);
	}
	cobjs_removed = 0;
	//PRINT_TIME("Purge");
}


void remove_all_coll_obj() {

	camera_coll_id = -1; // camera is special - keeps state

	for (int i = 0; i < MESH_Y_SIZE; ++i) {
		for (int j = 0; j < MESH_X_SIZE; ++j) {
			v_collision_matrix[i][j].clear(1);
			h_collision_matrix[i][j] = mesh_height[i][j];
		}
	}
	for (unsigned i = 0; i < coll_objects.size(); ++i) {
		if (coll_objects[i].status != COLL_UNUSED) free_index(i);
	}
}


int get_next_avail_index() {

	unsigned const old_size(coll_objects.size());
	assert(index_stack.size() == old_size);

	if (index_top >= old_size) {
		unsigned const new_size(2*index_top + 4); // double in size
		coll_objects.resize(new_size);
		index_stack.resize(new_size);

		for (unsigned i = old_size; i < new_size; ++i) { // initialize
			index_stack[i] = (int)i;
		}
	}
	int const index(index_stack[index_top]);
	assert(size_t(index) < coll_objects.size());
	assert(coll_objects[index].status == COLL_UNUSED);
	coll_objects[index].clear_lightmap(0); // invalidate cached objects for reused coll_obj
	coll_objects[index].status = COLL_PENDING;
	index_stack[index_top++]   = -1;
	return index;
}


void free_index(int index) {

	assert(coll_objects[index].status != COLL_UNUSED);
	coll_objects[index].status = COLL_UNUSED;

	if (!coll_objects[index].fixed) {
		coll_objects[index].shadow_depends.clear(); // ???
		assert(index_top > 0);
		index_stack[--index_top] = index;
	}
}


void set_coll_obj_props(int index, int type, float radius, float radius2, int platform_id, cobj_params const &cparams) {
	
	assert(size_t(index) < coll_objects.size());
	coll_obj &cobj(coll_objects[index]);
	cobj.status      = (cparams.is_dynamic ? COLL_DYNAMIC : COLL_STATIC);
	cobj.cp          = cparams;
	cobj.id          = index;
	cobj.radius      = radius;
	cobj.radius2     = radius2;
	cobj.type        = type;
	cobj.platform_id = platform_id;
	cobj.fixed       = 0;
	cobj.counter     = 0;
	cobj.destroy     = 1;
	cobj.calc_size();
	cobj.set_npoints();
	have_drawn_cobj    |= cparams.draw;
	have_platform_cobj |= (platform_id >= 0);
}


// only works for mesh collisions
int collision_detect_large_sphere(point &pos, float radius, unsigned flags) {

	int const xpos(get_xpos(pos.x)), ypos(get_ypos(pos.y));
	float const rdx(radius*DX_VAL_INV), rdy(radius*DY_VAL_INV);
	int const crdx((int)ceil(rdx)), crdy((int)ceil(rdy));
	int const x1(max(0, (xpos - (int)rdx))), x2(min(MESH_X_SIZE-1, (xpos + crdx)));
	int const y1(max(0, (ypos - (int)rdy))), y2(min(MESH_Y_SIZE-1, (ypos + crdy)));
	if (x1 > x2 || y1 > y2) return 0; // not sure if this can ever be false
	int const rsq(crdx*crdy);
	float const rsqf(radius*radius);
	bool const z_up(!(flags & (Z_STOPPED | FLOATING)));
	int coll(0);

	for (int i = y1; i <= y2; ++i) {
		float const yval(get_yval(i));
		int const y_dist((i - ypos)*(i - ypos));
		if (y_dist > rsq) continue; // can never satisfy condition below

		for (int j = x1; j <= x2; ++j) {
			if (y_dist + (j - xpos)*(j - xpos) > rsq) continue;
			point const mpt(get_xval(j), yval, mesh_height[i][j]);
			vector3d const v(pos, mpt);
			float const mag_sq(v.mag_sq());
			if (mag_sq >= rsqf) continue;
			float const old_z(pos.z), mag(sqrt(mag_sq));
			pos = mpt;

			if (mag < TOLERANCE) {
				pos.x += radius; // avoid divide by zero, choose x-direction (arbitrary)
			}
			else {
				pos += v*(radius/mag);
			}
			if (!z_up && pos.z >= old_z) pos.z = old_z;
			coll = 1; // return 1; ?
		}
	}
	assert(!is_nan(pos));
	return coll;
}


int check_legal_move(int x_new, int y_new, float zval, float radius, int &cindex) { // not dynamically updated

	if (point_outside_mesh(x_new, y_new)) return 0; // object out of simulation region
	coll_cell const &cell(v_collision_matrix[y_new][x_new]);
	if (cell.cvals.empty()) return 1;
	float const xval(get_xval(x_new)), yval(get_yval(y_new)), z1(zval - radius), z2(zval + radius);
	point const pval(xval, yval, zval);

	for (int k = (int)cell.cvals.size()-1; k >= 0; --k) { // iterate backwards
		int const index(cell.cvals[k]);
		if (index < 0) continue;
		assert(unsigned(index) < coll_objects.size());
		coll_obj &cobj(coll_objects[index]);
		if (cobj.no_collision()) continue;
		if (cobj.status == COLL_STATIC) {
			if (z1 > cell.zmax || z2 < cell.zmin) return 1; // should be OK here since this is approximate, not quite right with, but not quite right without
		}
		else continue; // smileys collision with dynamic objects can be handled by check_vert_collision()
		if (z1 > cobj.d[2][1] || z2 < cobj.d[2][0]) continue;
		bool coll(0);
		
		switch (cobj.type) {
		case COLL_CUBE:
			coll = ((pval.x + radius) >= cobj.d[0][0] && (pval.x - radius) <= cobj.d[0][1] &&
				    (pval.y + radius) >= cobj.d[1][0] && (pval.y - radius) <= cobj.d[1][1]);
			break;
		case COLL_SPHERE:
			coll = dist_less_than(pval, cobj.points[0], (cobj.radius + radius));
			break;
		case COLL_CYLINDER:
			coll = dist_xy_less_than(pval, cobj.points[0], (cobj.radius + radius));
			break;
		case COLL_CYLINDER_ROT:
			coll = (sphere_int_cylinder_sides(pval, radius, cobj.points[0], cobj.points[1], cobj.radius, cobj.radius2));
			break;
		case COLL_POLYGON: // must be coplanar
			{
				float thick, rdist;

				if (sphere_ext_poly_int_base(cobj.points[0], cobj.norm, pval, radius, cobj.thickness, thick, rdist)) {
					point const pos(pval - cobj.norm*rdist);
					assert(cobj.npoints > 0);
					coll = planar_contour_intersect(cobj.points, cobj.npoints, pos, cobj.norm);
				}
			}
			break;
		}
		if (coll) {
			cindex = index;
			return 0;
		}
	}
	return 1;
}


// ************ begin vert_coll_detector ************


bool proc_object_stuck(dwobject &obj, bool static_top_coll) {

	float const friction(object_types[obj.type].friction_factor);
	if (friction < 2.0*STICK_THRESHOLD || friction < rand_uniform(2.0, 3.0)*STICK_THRESHOLD) return 0;
	obj.flags |= (static_top_coll ? ALL_COLL_STOPPED : XYZ_STOPPED); // stuck in coll object
	obj.status = 4;
	return 1;
}


class vert_coll_detector {

	dwobject &obj;
	int type, iter;
	bool player, already_bounced;
	int coll, obj_index, do_coll_funcs;
	unsigned cdir, lcoll;
	float z_old, o_radius, z1, z2, c_zmax, c_zmin;
	point pos, pold;
	vector3d motion_dir, obj_vel;
	vector3d *cnorm;
	dwobject temp;

	bool safe_norm_div(float rad, float radius, vector3d &norm);
	void check_cobj(int index);
	void init_reset_pos();
public:
	vert_coll_detector(dwobject &obj_, int obj_index_, int do_coll_funcs_, int iter_,
		vector3d *cnorm_, vector3d const &mdir=zero_vector) :
	obj(obj_), type(obj.type), iter(iter_), player(type == CAMERA || type == SMILEY), already_bounced(0),
	coll(0), obj_index(obj_index_), do_coll_funcs(do_coll_funcs_), cdir(0),
	lcoll(0), z_old(obj.pos.z), cnorm(cnorm_), pos(obj.pos), pold(obj.pos), motion_dir(mdir), obj_vel(obj.velocity) {}
	int check_coll();
};


bool vert_coll_detector::safe_norm_div(float rad, float radius, vector3d &norm) {

	if (fabs(rad) < 10.0*TOLERANCE) {
		obj.pos.x += radius; // arbitrary
		norm.assign(1.0, 0.0, 0.0);
		return 0;
	}
	return 1;
}


void vert_coll_detector::check_cobj(int index) {

	coll_obj const &cobj(coll_objects[index]);
	if (cobj.no_collision())                         return; // collisions are disabled for this cobj
	if (type == PROJC    && obj.source  == cobj.id)  return; // can't shoot yourself with a projectile
	if (type == SMILEY   && obj.coll_id == cobj.id)  return; // can't collide with yourself
	if (type == LANDMINE && invalid_coll(obj, cobj)) return;
	float zmaxc(cobj.d[2][1]), zminc(cobj.d[2][0]);
	if (z1 > zmaxc || z2 < zminc)                    return;
	vector3d norm(zero_vector), pvel(zero_vector);
	bool const player_step(player && (camera_change || (cobj.d[2][1] - z1) <= o_radius*C_STEP_HEIGHT));
	bool coll_bot(0);
	
	if (cobj.platform_id >= 0) { // calculate platform velocity
		assert(cobj.platform_id < (int)platforms.size());
		pvel = platforms[cobj.platform_id].get_velocity();
	}
	vector3d const mdir(motion_dir - pvel*fticks); // not sure if this helps

	switch (cobj.type) { // within bounding box of collision object
	case COLL_CUBE:
		{
			float const xmax(cobj.d[0][1]), xmin(cobj.d[0][0]), ymax(cobj.d[1][1]), ymin(cobj.d[1][0]);
			if (pos.x < (xmin-o_radius) || pos.x > (xmax+o_radius)) break;
			if (pos.y < (ymin-o_radius) || pos.y > (ymax+o_radius)) break;
			if (o_radius > 0.9*LARGE_OBJ_RAD && !sphere_cube_intersect(pos, o_radius, cobj))        break;
			if (!sphere_cube_intersect(pos, o_radius, cobj, (pold - mdir), obj.pos, norm, cdir, 0)) break; // shouldn't get here much
			coll_bot = (cdir == 4);
			lcoll    = 1;
			//crcdir  |= (((cdir >> 1) + 1) % 3); // crcdir: x=1, y=2, z=0, cdir: -x0=0 +x=1 -y=2 +y=3 -z=4 +z=5

			if (cdir != 4 && cdir != 5 && player_step) {
				lcoll   = 0; // can step up onto the object
				obj.pos = pos; // reset pos
				norm    = zero_vector;
				break;
			}
			if (cdir == 5) { // +z collision
				if (cobj.contains_pt_xy(pos)) ++lcoll;
				float const rdist(max(max(max((pos.x-(xmax+o_radius)), ((xmin-o_radius)-pos.x)), (pos.y-(ymax+o_radius))), ((ymin-o_radius)-pos.y)));
				
				if (rdist > 0.0) {
					obj.pos.z -= o_radius;
					if (o_radius > rdist) obj.pos.z += sqrt(o_radius*o_radius - rdist*rdist);
				}
				break;
			}
		}
		break;

	case COLL_SPHERE:
		{
			float const radius(cobj.radius + o_radius);
			float rad(p2p_dist_sq(pos, cobj.points[0])), reff(radius);

			if (player && cobj.cp.coll_func == landmine_collision) {
				reff += 1.5*object_types[type].radius; // landmine
			}
			if (rad <= reff*reff) {
				lcoll = 1;
				rad   = sqrt(rad);
				if (!safe_norm_div(rad, radius, norm)) break;
				norm  = (pos - cobj.points[0])/rad;

				if (rad <= radius) {
					obj.pos = cobj.points[0] + norm*radius;
					assert(!is_nan(obj.pos));
				}
			}
		}
		break;

	case COLL_CYLINDER:
		{
			point const center(cobj.get_center_pt());
			float rad(p2p_dist_xy_sq(pos, center)), radius(cobj.radius); // rad is xy dist

			if (rad <= (radius + o_radius)*(radius + o_radius)) {
				rad    = sqrt(rad);
				lcoll  = 1;
				zmaxc += o_radius;
				zminc -= o_radius;
				float const pozm(pold.z - mdir.z);

				if (!(cobj.cp.surfs & 1) && pozm > (zmaxc - SMALL_NUMBER) && pos.z <= zmaxc) { // collision with top
					if (rad <= radius) ++lcoll;
					norm.assign(0.0, 0.0, 1.0);
					float const rdist(rad - radius);
					obj.pos.z = zmaxc;
					
					if (rdist > 0.0) {
						obj.pos.z -= o_radius;
						if (o_radius >= rdist) obj.pos.z += sqrt(o_radius*o_radius - rdist*rdist);
					}
				}
				else if (!(cobj.cp.surfs & 1) && pozm < (zminc + SMALL_NUMBER) && pos.z >= zminc) { // collision with bottom
					norm.assign(0.0, 0.0, -1.0);
					obj.pos.z = zminc - o_radius;
					coll_bot  = 1;
				}
				else { // collision with side
					if (player_step && obj.pos.z > cobj.d[2][1]) {
						norm = plus_z;
						break; // OK, can step up onto cylinder
					}
					radius += o_radius;
					if (!safe_norm_div(rad, radius, norm)) break;
					norm.assign((pos.x - center.x)/rad, (pos.y - center.y)/rad, 0.0);
					for (unsigned d = 0; d < 2; ++d) obj.pos[d] = center[d] + norm[d]*radius;
				}
			}
		}
		break;

	case COLL_CYLINDER_ROT:
		if (sphere_intersect_cylinder_ipt(pos, o_radius, cobj.points[0], cobj.points[1],
			cobj.radius, cobj.radius2, !(cobj.cp.surfs & 1), obj.pos, norm, 1)) lcoll = 1;
		break;

	case COLL_POLYGON: // must be coplanar
		{
			float thick, rdist, val;
			norm = cobj.norm;
			//float const dp(dot_product_ptv(norm, (pold - mdir), pos);
			float const dp(-dot_product_ptv(norm, (pold - mdir), cobj.points[0]));
			if (dp > 0.0) norm.negate();

			if (sphere_ext_poly_int_base(cobj.points[0], norm, pos, o_radius, cobj.thickness, thick, rdist)) {
				//if (rdist < 0) {rdist = -rdist; norm.negate();}

				if (sphere_poly_intersect(cobj.points, cobj.npoints, pos, norm, rdist, max(0.0f, (thick - MIN_POLY_THICK2)))) {
					if (cobj.thickness > MIN_POLY_THICK2) { // compute norm based on extruded sides
						vector<vector<point> > const &pts(thick_poly_to_sides(cobj.points, cobj.npoints, cobj.norm, cobj.thickness));
						if (!sphere_intersect_poly_sides(pts, pos, o_radius, val, norm, 1)) break; // no collision
						bool intersects(0), inside(1);
						
						for (unsigned i = 0; i < pts.size(); ++i) { // inefficient and inexact, but closer to correct
							vector3d const norm2(get_poly_norm(&pts[i].front()));
							float rdist2(dot_product_ptv(norm2, pos, cobj.points[0]));
							
							if (sphere_poly_intersect(&pts[i].front(), pts[i].size(), pos, norm2, rdist2, o_radius)) {
								intersects = 1;
								break;
							}
							if (rdist2 > 0.0) inside = 0;
						}
						if (!intersects && !inside) break; // doesn't intersect any face and isn't completely inside
					}
					else {
						val = 1.01*(thick - rdist); // non-thick polygon
					}
					assert(!is_nan(norm));
					obj.pos += norm*val; // calculate intersection point
					lcoll    = 1;
				} // end sphere poly int
			} // rnf sphere int check
		} // end COLL_POLY scope
		break;
	} // switch
	if (lcoll) {
		assert(norm != zero_vector);
		assert(!is_nan(norm));
		bool is_moving(0);

		// collision with the top of a cube attached to a platform (on first iteration only)
		if (cobj.platform_id >= 0) {
			assert(cobj.platform_id < (int)platforms.size());
			is_moving = (lcoll == 2);

			if (animate2 && do_coll_funcs && iter == 0) {
				if (lcoll == 2) { // move with the platform (clip v if large -z?)
					obj.pos += platforms[cobj.platform_id].get_last_delta();
				}
				else if (coll_bot && platforms[cobj.platform_id].get_last_delta().z < 0.0) {
					if (type == CAMERA || type == SMILEY) {
						int const ix((type == CAMERA) ? -1 : obj_index);
						smiley_collision(ix, -2, vector3d(0.0, 0.0, -1.0), pos, 2000.0, CRUSHED); // lots of damage
					} // other objects?
				}
			}
			// reset last pos (init_dir) if object is only moving on a platform
			bool const platform_moving(platforms[cobj.platform_id].is_moving());
			//if (type == BALL && platform_moving) obj.init_dir = obj.pos;
			if (platform_moving) obj.flags |= PLATFORM_COLL;
		}
		if (animate2 && type != CAMERA && type != SMILEY && obj.health <= 0.1) obj.disable();
		vector3d v_old(zero_vector), v0(obj.velocity);
		obj_type const &otype(object_types[type]);
		float const friction(otype.friction_factor);
		bool const static_top_coll(lcoll == 2 && cobj.truly_static());

		if (is_moving || friction < STICK_THRESHOLD) {
			v_old = obj.velocity;

			if (otype.elasticity == 0.0 || cobj.cp.elastic == 0.0 || !obj.object_bounce(3, norm, cobj.cp.elastic, pos.z, 0.0, pvel)) {
				if (static_top_coll) {
					obj.flags |= STATIC_COBJ_COLL; // collision with top
					if (otype.flags & OBJ_IS_DROP) obj.velocity = zero_vector;
				}
				if (type != DYNAM_PART && obj.velocity != zero_vector) {
					assert(TIMESTEP > 0.0);
					if (friction > 0.0) obj.velocity *= (1.0 - min(1.0f, (tstep/TIMESTEP)*friction)); // apply kinetic friction
					//for (unsigned i = 0; i < 3; ++i) obj.velocity[i] *= (1.0 - fabs(norm[i])); // norm must be normalized
					orthogonalize_dir(obj.velocity, norm, obj.velocity, 0); // rolling friction model
				}
			}
			else if (already_bounced) {
				obj.velocity = v_old; // can only bounce once
			}
			else {
				already_bounced = 1;
			}
		}
		else { // sticks
			if (cobj.status == COLL_STATIC) {
				if (!proc_object_stuck(obj, static_top_coll) && static_top_coll) obj.flags |= STATIC_COBJ_COLL; // coll with top
				obj.pos -= norm*(0.1*o_radius); // make sure it still intersects
			}
			obj.velocity = zero_vector; // I think this is correct
		}
		// only use cubes for now, because leaves colliding with tree leaves and branches and resetting the normals is too unstable
		if ((otype.flags & OBJ_IS_FLAT) && cobj.type == COLL_CUBE) obj.set_orient_for_coll(&norm);
		
		if (do_coll_funcs && cobj.cp.coll_func != NULL) { // call collision function
			invalid_collision = 0; // should already be 0
			float energy_mult(1.0);
			if (type == PLASMA) energy_mult *= obj.init_dir.x*obj.init_dir.x; // size squared
			float const energy(get_coll_energy(v_old, obj.velocity, otype.mass));
			cobj.cp.coll_func(cobj.cp.cf_index, obj_index, v_old, obj.pos, energy_mult*energy, type);
			
			if (invalid_collision) { // reset local collision
				invalid_collision = 0;
				lcoll = 0;
				obj   = temp;
				return;
			}
		}
		if (!(otype.flags & OBJ_IS_DROP) && type != LEAF && type != CHARRED && type != SHRAPNEL &&
			type != BEAM && type != LASER && type != FIRE && type != SMOKE && type != PARTICLE)
		{
			coll_objects[index].register_coll(TICKS_PER_SECOND, IMPACT);
		}
		obj.verify_data();
		
		if (!obj.disabled() && (otype.flags & EXPL_ON_COLL)) {
			if (cobj.type == COLL_CUBE && cobj.can_be_scorched()) {
				int const dir(cdir >> 1), ds((dir+1)%3), dt((dir+2)%3);
				float const sz(5.0*otype.radius);
				float const dmin(min(min((cobj.d[ds][1] - obj.pos[ds]), (obj.pos[ds] - cobj.d[ds][0])),
					                 min((cobj.d[dt][1] - obj.pos[dt]), (obj.pos[dt] - cobj.d[dt][0]))));

				if (dmin > sz) gen_scorch_mark((obj.pos - norm*o_radius), sz, norm, 0.75);
			}
			obj.disable();
		}
		deform_obj(obj, norm, v0);
		if (cnorm != NULL) *cnorm = norm;
		obj.flags |= OBJ_COLLIDED;
		coll      |= lcoll; // if not an invalid collision
		lcoll      = 0; // reset local collision
		init_reset_pos(); // reset local state
		if (friction < STICK_THRESHOLD) return;
		if (obj.flags & Z_STOPPED) obj.pos.z = pos.z = z_old;
	} // if lcoll
}


void vert_coll_detector::init_reset_pos() {

	temp  = obj; // backup copy
	pos   = obj.pos; // reset local state
	z1    = pos.z - o_radius;
	z2    = pos.z + o_radius;
}


int vert_coll_detector::check_coll() {

	int const xpos(get_xpos(obj.pos.x)), ypos(get_ypos(obj.pos.y));
	if (point_outside_mesh(xpos, ypos)) return 0; // object along edge
	coll_cell const &cell(v_collision_matrix[ypos][xpos]);
	if (cell.cvals.empty()) return 0;
	point const porig(pos);
	pold -= obj.velocity*tstep;
	assert(!is_nan(pold));
	assert(type >= 0 && type < NUM_TOT_OBJS);
	o_radius = ((type == PLASMA) ? obj.init_dir.x : 1.0)*object_types[type].radius;
	c_zmax   = cell.zmax;
	c_zmin   = cell.zmin;
	init_reset_pos();
	bool subdiv(!cell.cvz.empty());

	for (int k = int(cell.cvals.size())-1; k >= 0; --k) { // iterate backwards
		// Can get here when check_cobj() causes more than one object to be removed and cell.cvals is reduced by more than 1
		if (unsigned(k) >= cell.cvals.size()) continue;
		unsigned const index(cell.cvals[k]);
		if (index < 0) continue;
		assert(index < coll_objects.size());

		if (coll_objects[index].status == COLL_STATIC) {
			if (ALWAYS_ADD_TO_HCM) assert(c_zmax >= c_zmin);

			// This is a big performance optimization, but isn't quite right in all cases,
			// so don't use it if something important like a smiley or the player is involved
			if (type != SMILEY && type != CAMERA) {
				if (o_radius < HALF_DXY && (z1 > c_zmax || z2 < c_zmin)) {subdiv = 0; break;}
			}
			if (subdiv) break;
		}
		check_cobj(index);
	}
	if (subdiv) {
		++cobj_counter;
		unsigned const sz(cell.cvz.size());
		float const val(sz/(c_zmax - c_zmin));
		unsigned const zs(min(sz-1, (unsigned)max(0, (int)floor(val*(z1 - c_zmin)))));
		unsigned const ze(min(sz-1, (unsigned)max(0, (int)floor(val*(z2 - c_zmin)))));

		// calculate loop bounds
		for (unsigned i = zs; i <= ze; ++i) {
			vector<int> const &cvz(cell.cvz[i]);

			for (unsigned z = 0; z < cvz.size(); ++z) {
				unsigned const index(cvz[z]);
				if (index < 0) continue;
				assert(index < coll_objects.size());
				if (coll_objects[index].counter == cobj_counter) continue; // prevent duplicate testing of cobjs
				coll_objects[index].counter = cobj_counter;
				check_cobj(index);
			}
		}
	}
	return coll;
}


// ************ end vert_coll_detector ************


// 0 = non vert coll, 1 = X coll, 2 = Y coll, 3 = X + Y coll
int dwobject::check_vert_collision(int obj_index, int do_coll_funcs, int iter, vector3d *cnorm, vector3d const &mdir) {

	vert_coll_detector vcd(*this, obj_index, do_coll_funcs, iter, cnorm, mdir);
	return vcd.check_coll();
}


int dwobject::multistep_coll(point const &last_pos, int obj_index, unsigned nsteps) {

	int any_coll(0);
	vector3d cmove(pos, last_pos);
	float const dist(cmove.mag()); // 0.018

	if (dist < 1.0E-6 || nsteps == 1) {
		any_coll |= check_vert_collision(obj_index, 1, 0); // collision detection
	}
	else {
		float const step(dist/(float)nsteps);
		vector3d const dpos(pos, last_pos);
		cmove /= dist;
		pos    = last_pos; // Note: can get stuck in this position if forced off the mesh by a collision

		for (unsigned i = 0; i < nsteps && !disabled(); ++i) {
			pos      += cmove*step;
			any_coll |= check_vert_collision(obj_index, (i==nsteps-1), 0, NULL, dpos); // collision detection
		}
	}
	return any_coll;
}


void add_camera_cobj(point const &pos) {

	camera_coll_id = add_coll_sphere(pos, CAMERA_RADIUS,
		cobj_params(object_types[CAMERA].elasticity, object_types[CAMERA].color, 0, 1, camera_collision, 1));
}


void force_onto_surface_mesh(point &pos) { // for camera

	bool const cflight(game_mode && flight);
	int coll(0);
	float radius(CAMERA_RADIUS);
	dwobject camera_obj(def_objects[CAMERA]); // make a fresh copy

	if (!cflight) { // make sure camera is over simulation region
		camera_in_air = 0;
		clip_to_scene(pos);
	}
	remove_coll_object(camera_coll_id);
	camera_coll_id = -1;
	camera_obj.pos = pos;
	camera_obj.velocity.assign(0.0, 0.0, -1.0);
	//camera_obj.velocity += (pos - camera_last_pos)/tstep; // ???

	if (world_mode == WMODE_GROUND) {
		unsigned const nsteps(CAMERA_STEPS); // *** make the number of steps determined by fticks? ***
		coll  = camera_obj.multistep_coll(camera_last_pos, 0, nsteps);
		pos.x = camera_obj.pos.x;
		pos.y = camera_obj.pos.y;
		if (!cflight) clip_to_scene(pos);
	}
	else if (!cflight) {
		pos.z           = int_mesh_zval_pt_off(pos, 1, 0) + radius;
		camera_last_pos = pos;
		camera_change   = 0;
		return; // infinite terrain mode
	}
	if (cflight) {
		if (coll) pos.z = camera_obj.pos.z;
		float const mesh_z(int_mesh_zval_pt_off(pos, 1, 0));
		pos.z = min((camera_last_pos.z + float(C_STEP_HEIGHT*radius)), pos.z); // don't fall and don't rise too quickly
		if (pos.z + radius > zbottom) pos.z = max(pos.z, (mesh_z + radius)); // if not under the mesh
	}
	if (camera_zh > 0.0) { // prevent head collisions with the ceiling
		vector3d const dpos(pos, camera_last_pos);
		camera_obj.pos    = pos;
		camera_obj.pos.z += camera_zh;
		camera_obj.check_vert_collision(0, 0, 0, NULL, dpos);
		camera_obj.pos.z -= camera_zh;
		pos               = camera_obj.pos;
	}
	if (!cflight) {
		if (point_outside_mesh((get_xpos(pos.x) - xoff), (get_ypos(pos.y) - yoff))) {
			pos = camera_last_pos;
			camera_change = 0;
			return;
		}
		set_true_obj_height(pos, camera_last_pos, C_STEP_HEIGHT, sstates[CAMERA_ID].zvel, CAMERA, CAMERA_ID, cflight, camera_on_snow); // status return value is unused?
	}
	camera_on_snow = 0;

	if (display_mode & 0x10) { // walk on snow
		float zval;
		vector3d norm;
		
		if (get_snow_height(pos, radius, zval, norm, 1)) {
			pos.z = zval + radius;
			camera_on_snow = 1;
			camera_in_air  = 0;
		}
	}
	if (camera_coll_smooth) collision_detect_large_sphere(pos, radius, (unsigned char)0);
	if (temperature > W_FREEZE_POINT && is_underwater(pos, 1) && (rand()&1)) gen_bubble(pos);
	
	if (!cflight && camera_change == 0 && camera_last_pos.z != 0.0 && (pos.z - camera_last_pos.z) > CAMERA_MESH_DZ &&
		is_above_mesh(pos) && is_over_mesh(camera_last_pos))
	{
		pos = camera_last_pos; // mesh is too steep for camera to climb
	}
	else {
		camera_last_pos = pos;
	}
	if (camera_change == 1) {
		camera_last_pos = pos;
		camera_change   = 2;
	}
	else {
		camera_change = 0;
	}
	point pos2(pos);
	pos2.z += 0.5*camera_zh;
	add_camera_cobj(pos2);
}


// 0 = no change, 1 = moved up, 2 = falling, 3 = stuck
int set_true_obj_height(point &pos, point const &lpos, float step_height, float &zvel, int type, int id, bool flight, bool on_snow) {

	int const xpos(get_xpos(pos.x) - xoff), ypos(get_ypos(pos.y) - yoff);
	bool const is_camera(type == CAMERA), is_player(is_camera || type == SMILEY);

	if (point_outside_mesh(xpos, ypos)) {
		if (is_player) sstates[id].fall_counter = 0;
		zvel = 0.0;
		return 0;
	}
	float const g_acc(base_gravity*GRAVITY*tstep*object_types[type].gravity);
	float const terminal_v(object_types[type].terminal_vel), radius(object_types[type].radius);
	float const step(step_height*radius), mh(int_mesh_zval_pt_off(pos, 1, 0)); // *** step height determined by fticks? ***
	pos.z = max(pos.z, (mh + radius));

	if (display_mode & 0x10) { // walk on snow (smiley and camera, though doesn't actually set smiley z value correctly)
		float zval;
		vector3d norm;
		if (get_snow_height(pos, radius, zval, norm, 1)) pos.z = zval + radius;
	}
	float zmu(mh), z1(pos.z - radius), z2(pos.z + radius);
	if (is_camera) z2 += camera_zh; // add camera height
	coll_cell const &cell(v_collision_matrix[ypos][xpos]);
	int coll(0), any_coll(0), moved(0);
	float zceil, zfloor, zt, zb;
	bool falling(0);

	for (int k = (int)cell.cvals.size()-1; k >= 0; --k) { // iterate backwards
		int const index(cell.cvals[k]);
		if (index < 0) continue;
		assert(unsigned(index) < coll_objects.size());
		coll_obj const &cobj(coll_objects[index]);
		if (cobj.no_collision()) continue;
		
		switch (cobj.type) {
		case COLL_CUBE:
			if (cobj.contains_pt_xy(pos)) {
				zt   = cobj.d[2][1];
				zb   = cobj.d[2][0];
				coll = 1;
			}
			break;

		case COLL_CYLINDER:
			if (dist_xy_less_than(pos, cobj.points[0], cobj.radius)) {
				zt   = cobj.d[2][1];
				zb   = cobj.d[2][0];
				coll = 1;
			}
			break;

		case COLL_SPHERE:
			{
				vector3d const vtemp(pos, cobj.points[0]);

				if (vtemp.mag_sq() <= cobj.radius*cobj.radius) {
					float const arg(cobj.radius*cobj.radius - vtemp.x*vtemp.x - vtemp.y*vtemp.y);
					assert(arg >= 0.0);
					float const sqrt_arg(sqrt(arg));
					zt   = cobj.points[0].z + sqrt_arg;
					zb   = cobj.points[0].z - sqrt_arg;
					coll = 1;
				}
			}
			break;

		case COLL_CYLINDER_ROT:
			{
				float t, rad;
				vector3d v1, v2;

				if (sphere_int_cylinder_pretest(pos, radius, cobj.points[0], cobj.points[1], cobj.radius, cobj.radius2, 0, v1, v2, t, rad)) {
					float const rdist(v2.mag());
					
					if (fabs(rdist) > TOLERANCE) {
						float const val(fabs((rad/rdist - 1.0)*v2.z));
						zt   = pos.z + val;
						zb   = pos.z - val;
						coll = 1;
					}
				}
			}
			break;

		case COLL_POLYGON: // must be coplanar, may not work correctly if vertical (but check_vert_collision() should take care of that case)
			{
				coll = 0;
				float const thick(0.5*cobj.thickness);
				bool const poly_z(fabs(cobj.norm.z) > 0.5); // hack to fix bouncy polygons and such - should fix better eventually

				if (cobj.thickness > MIN_POLY_THICK2 && !poly_z) {
					float val;
					vector3d norm;
					vector<vector<point> > const &pts(thick_poly_to_sides(cobj.points, cobj.npoints, cobj.norm, cobj.thickness));
					if (!sphere_intersect_poly_sides(pts, pos, radius, val, norm, 0)) break; // no collision
					float const zminc(cobj.d[2][0]), zmaxc(cobj.d[2][1]);
					zb = zmaxc;
					zt = zminc;

					if (get_poly_zvals(pts, pos.x, pos.y, zb, zt)) {
						zb   = max(zminc, zb);
						zt   = min(zmaxc, zt);
						coll = (zb < zt);
					}
				}
				else if (point_in_polygon_2d(pos.x, pos.y, cobj.points, cobj.npoints, 0, 1)) {
					float const rdist(dot_product_ptv(cobj.norm, pos, cobj.points[0]));
					// works best if the polygon has a face oriented in +z or close
					// note that we don't care if the polygon is intersected in z
					zt   = pos.z + cobj.norm.z*(-rdist + thick);
					zb   = pos.z + cobj.norm.z*(-rdist - thick);
					coll = 1;
					if (zt < zb) swap(zt, zb);
				}
				// clamp to actual polygon bounds (for cases where the object intersects the polygon's plane but not the polygon)
				zt = max(cobj.d[2][0], min(cobj.d[2][1], zt));
				zb = max(cobj.d[2][0], min(cobj.d[2][1], zb));
			}
			break;
		} // end switch
		if (cobj.platform_id >= 0) zt -= 1.0E-6; // subtract a small value so that camera still collides with cobj

		if (coll) {
			if (zt < zb) {
				cout << "type = " << int(cobj.type) << ", zb = " << zb << ", zt = " << zt << ", pos.z = " << pos.z << endl;
				assert(0);
			}
			if (zt <= z1) zmu = max(zmu, zt);
			
			if (any_coll) {
				zceil  = max(zceil,  zt);
				zfloor = min(zfloor, zb);
			}
			else {
				zceil  = zt;
				zfloor = zb;
			}
			if (z2 > zb && z1 < zt) { // top of object above bottom of surface and bottom of object below top of surface
				if ((zt - z1) <= step) { // step up onto surface
					pos.z = max(pos.z, zt + radius);
					zmu   = max(zmu, zt);
					moved = 1;
				}
				else if (is_camera && camera_change) {
					pos.z = zt + radius;
					zmu   = max(zmu, zt);
				}
				else { // stuck against side of surface
					if (pos.z > zb) { // head inside the object
						if (is_player) sstates[id].fall_counter = 0;
						pos  = lpos; // reset to last known good position
						zvel = 0.0;
						return 3;
					}
					else { // fall down below zb - can recover
						pos.z = zb - radius;
					}
				}
			}
			any_coll = 1;
		}
	}
	if (!any_coll || z2 < zfloor) {
		pos.z = mh;
		bool const on_ice(is_camera && (camera_coll_smooth || game_mode) && temperature <= W_FREEZE_POINT && is_underwater(pos));
		if (on_ice) pos.z = water_matrix[ypos][xpos]; // standing on ice
		pos.z += radius;
		if (!on_ice) modify_grass_at(pos, radius, (type != FIRE), (type == FIRE), 0, 0);
	}
	else {
		zceil = max(zceil, mh);

		if (z1 > zceil) { // bottom of object above all top surfaces
			pos.z = zceil + radius; // object falls to the floor
		}
		else {
			pos.z = zmu + radius;
		}
	}
	if ((is_camera && camera_change) || mesh_scale_change || on_snow) {
		zvel = 0.0;
	}
	else if ((pos.z - lpos.z) < -step) { // falling through the air
		falling = 1;
	}
	else {
		zvel = 0.0;
	}
	if (is_camera) {
		if (falling)        camera_in_air     = 1;
		if (!camera_in_air) camera_invincible = 0;
	}
	if (flight) {
		zvel = 0.0;
		if (is_player) sstates[id].fall_counter = 0;
	}
	else if (falling) {
		zvel  = max(-terminal_v, (zvel - g_acc));
		pos.z = max(pos.z, (lpos.z + tstep*zvel));

		if (is_player) {
			if (sstates[id].fall_counter == 0) sstates[id].last_dz = 0.0;
			++sstates[id].fall_counter;
			sstates[id].last_dz  += (pos.z - lpos.z);
			sstates[id].last_zvel = zvel;
		}
	}
	else if (is_player) { // falling for several frames continuously and finally stops
		if (sstates[id].fall_counter > 4 && sstates[id].last_dz < 0.0 && sstates[id].last_zvel < 0.0) player_fall(id);
		sstates[id].fall_counter = 0;
	}
	return moved;
}



