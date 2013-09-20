// 3D World - OpenGL CS184 Computer Graphics Project
// by Frank Gennari
// 9/1/02


#include "3DWorld.h"
#include "mesh.h"
#include "textures_3dw.h"
#include "sinf.h"


int      const NUM_FREQ_COMP      = 9;
int      const NUM_L0_FREQ_COMP   = 6;
float    const I_ATTEN_VAL        = 0.25;
float    const ISLAND_SHAPE_EXP   = 1.75;
float    const ISLAND_MAG_SCALE   = 1.6;
float    const ISLAND_FREQ_SCALE  = 2.5;
float    const SHAPE_PERIOD_SCALE = 1.0;
float    const MESH_SCALE_Z_EXP   = 0.7;
int      const N_RAND_SIN2        = 10;
int      const FREQ_FILTER        = 2; // higher = smoother landscape
int      const MIN_FREQS          = 3;
int      const FREQ_RANGE         = 80; // higher for more accurate mesh - should be 80
int      const NUM_LOW_FREQ       = 2;
float    const W_PLANE_Z          = 0.42;
float    const HEIGHT_SCALE       = 0.01;
unsigned const EST_RAND_PARAM     = 128;
float    const READ_MESH_H_SCALE  = 0.0008;
bool     const AUTOSCALE_HEIGHT   = 1;
bool     const DEF_GLACIATE       = 1;
float    const GLACIATE_EXP       = 3.0;
bool     const GEN_SCROLLING_MESH = 1;
float    const S_GEN_ATTEN_DIST   = 128.0;

int   const F_TABLE_SIZE       = NUM_FREQ_COMP*N_RAND_SIN2;
int   const START_L0_FREQ_COMP = NUM_FREQ_COMP - NUM_L0_FREQ_COMP;

#define DO_GLACIATE_EXP(val) ((val)*(val)*(val)) // GLACIATE_EXP = 3.0


// Global Variables
float MESH_START_MAG(0.02), MESH_START_FREQ(240.0), MESH_MAG_MULT(2.0), MESH_FREQ_MULT(0.5);
int cache_counter(1), start_eval_sin(0), end_eval_sin(0), GLACIATE(DEF_GLACIATE);
float zmax, zmin, zmax_est, zcenter(0.0), zbottom(0.0), ztop(0.0), h_sum(0.0), alt_temp(DEF_TEMPERATURE);
float mesh_scale(1.0), tree_scale(1.0), mesh_scale_z(1.0), glaciate_exp(1.0), glaciate_exp_inv(1.0);
float mesh_height_scale(1.0), zmm_calc(1.0), zmax_est2(1.0), zmax_est2_inv(1.0);
float *sin_table(NULL), *cos_table(NULL);
float sinTable[F_TABLE_SIZE][5];

int const mesh_tids_sand[NTEX_SAND] = {SAND_TEX, GROUND_TEX, ROCK_TEX,   SNOW_TEX};
int const mesh_tids_dirt[NTEX_DIRT] = {SAND_TEX, DIRT_TEX,   GROUND_TEX, ROCK_TEX, SNOW_TEX};
float const mesh_rh_sand[NTEX_SAND] = {0.18, 0.40, 0.70, 1.0};
float const mesh_rh_dirt[NTEX_DIRT] = {0.40, 0.44, 0.60, 0.75, 1.0};
float sthresh[2][2][2] = {{{0.68, 0.86}, {0.48, 0.72}}, {{0.5, 0.7}, {0.3, 0.55}}}; // {normal, island}, {grass, snow}, {lo, hi}
ttex lttex_sand[NTEX_SAND], lttex_dirt[NTEX_DIRT];


extern bool combined_gu;
extern int island, xoff, yoff, xoff2, yoff2, world_mode, resolution, rand_gen_index, mesh_scale_change;
extern int read_heightmap, read_landscape, do_read_mesh, mesh_seed, scrolling, camera_mode, invert_mh_image;
extern double c_radius, c_phi, c_theta;
extern float water_plane_z, temperature, mesh_file_scale, mesh_file_tz, MESH_HEIGHT, XY_SCENE_SIZE;
extern float water_h_off, water_h_off_rel, disabled_mesh_z, read_mesh_zmm, init_temperature, univ_temp;
extern point mesh_origin, surface_pos;
extern char *mh_filename, *dem_filename, *mesh_file;
extern rand_gen_t global_rand_gen;


void glaciate();
void gen_terrain_map();
void estimate_zminmax(bool using_eq);
void set_zvals();
void update_temperature(bool verbose);
void compute_scale(int make_island);



void create_sin_table() {

	if (sin_table != NULL) return; // already setup
	sin_table = new float[2*TSIZE];
	cos_table = sin_table + TSIZE;

	for (unsigned i = 0; i < TSIZE; ++i) {
		sin_table[i] = sinf(i/sscale);
		cos_table[i] = cosf(i/sscale);
	}
}


void matrix_min_max(float **matrix, float &minval, float &maxval) {

	assert(matrix);
	minval = maxval = matrix[0][0];

	for (int i = 0; i < MESH_Y_SIZE; ++i) {
		for (int j = 0; j < MESH_X_SIZE; ++j) {
			minval = min(minval, matrix[i][j]);
			maxval = max(maxval, matrix[i][j]);
		}
	}
}

void calc_zminmax() {

	matrix_min_max(mesh_height, zmin, zmax);
}


bool bmp_to_chars(char *fname, char **&data) { // Note: supports all image formats

	assert(fname != NULL);
	texture_t texture(0, 7, MESH_X_SIZE, MESH_Y_SIZE, 0, 1, 0, fname, 0); // invert_y=0
	texture.load(-1); // generates fatal internal errors if load() fails, so return is always 1
	assert(texture.width == MESH_X_SIZE && texture.height == MESH_Y_SIZE); // could make into an error
	matrix_gen_2d(data);
	memcpy(data[0], texture.get_data(), XY_MULT_SIZE*sizeof(char));
	texture.free_data();
	return 1;
}


// Note: only works for 8-bit heightmaps (higher precision textures are truncated)
bool read_mesh_height_image(char const *fn, bool allow_resize=1) {

	if (fn == NULL) {
		std::cerr << "Error: No mh_filename spcified in the config file." << endl;
		return 0;
	}
	cout << "Reading mesh hieghtmap " << mh_filename << endl;
	texture_t texture(0, 7, MESH_X_SIZE, MESH_Y_SIZE, 0, 1, 0, fn, (invert_mh_image != 0));
	texture.load(-1, allow_resize, 1); // allow 2-byte grayscale (currently only works for PNGs)
	if (allow_resize) {texture.resize(MESH_X_SIZE, MESH_Y_SIZE);}
		
	if (texture.width != MESH_X_SIZE || texture.height != MESH_Y_SIZE) { // may be due to texture padding to make a multipe of 4
		std::cerr << "Error reading mesh height image: Expected size " << MESH_X_SIZE << "x" << MESH_Y_SIZE
				    << ", got size " << texture.width << "x" << texture.height << endl;
		return 0;
	}
	unsigned char const *data(texture.get_data());
	assert(data);
	float const mh_scale(READ_MESH_H_SCALE*mesh_file_scale*mesh_height_scale);
	assert(texture.ncolors == 1 || texture.ncolors == 2); // one or two byte grayscale
	unsigned ix(0);

	for (int i = 0; i < MESH_Y_SIZE; ++i) {
		for (int j = 0; j < MESH_X_SIZE; ++j) {
			float const val((texture.ncolors == 2) ? ((data[ix] << 8) + data[ix+1])/256.0 : data[ix]);
			mesh_height[i][j] = mh_scale*val + mesh_file_tz;
			ix += texture.ncolors;
		}
	}
	texture.free_data();
	return 1;
}


void set_zmax_est(float zval) {

	zmax_est      = zval;
	zmax_est2     = 2.0*zmax_est;
	zmax_est2_inv = 1.0/zmax_est2;
}


void update_disabled_mesh_height() {

	if (disabled_mesh_z == FAR_CLIP || mesh_draw == NULL) return;

	for (int i = 0; i < MESH_Y_SIZE; ++i) {
		for (int j = 0; j < MESH_X_SIZE; ++j) {
			if (is_mesh_disabled(j, i)) {mesh_height[i][j] = disabled_mesh_z;}
		}
	}
}


inline void clamp_to_mesh(int xy[2]) {

	for (unsigned i = 0; i < 2; ++i) {
		xy[i] = max(0, min(MESH_SIZE[i]-1, xy[i]));
	}
}


void gen_mesh_random(float height) {

	for (int i = 0; i < MESH_Y_SIZE; ++i) {
		for (int j = 0; j < MESH_X_SIZE; ++j) {
			float val(signed_rand_float2()*height);
			if (i + j == 0) val *= 10.0;
			if (i != 0)     val += 0.45*mesh_height[i-1][j];
			if (j != 0)     val += 0.45*mesh_height[i][j-1];
			mesh_height[i][j] = val;
		}
	}
}



void gen_mesh_sine_table(float **matrix, int x_offset, int y_offset, int xsize, int ysize, int make_island) {

	assert(matrix);
	int const num_freq(make_island ? NUM_L0_FREQ_COMP : NUM_FREQ_COMP), nsines(num_freq*N_RAND_SIN2);
	float hoff(0.0);
	int i2(y_offset - ysize/2), j2(x_offset - xsize/2);
	vector<float> jTerms(xsize*(end_eval_sin - start_eval_sin));

	if (end_eval_sin < nsines) {
		float const iscale(mesh_scale*i2), jscale(mesh_scale*j2);

		for (int k = end_eval_sin; k < nsines; ++k) { // approximate low frequencies as constant
			float const *stk(sinTable[k]);
			hoff += stk[0]*sinf(stk[3]*iscale + stk[1])*sinf(stk[4]*jscale + stk[2]);
		}
		hoff /= mesh_scale_z;
	}
	for (int k = start_eval_sin, ix = 0; k < end_eval_sin; ++k) {
		float const *stk(sinTable[k]);

		for (int j = 0; j < xsize; ++j) {
			jTerms[ix++] = sinf(mesh_scale*stk[4]*(j+j2) + stk[2]);
		}
	}
	for (int i = 0; i < ysize; ++i) {
		++i2;
		float const si2(mesh_scale*i2);

		for (int k = start_eval_sin, ix = 0; k < end_eval_sin; ++k) {
			float const *stk(sinTable[k]);
			float const val((stk[0]/mesh_scale_z)*sinf(stk[3]*si2 + stk[1]));

			for (int j = 0; j < xsize; ++j) {
				matrix[i][j] += val*jTerms[ix++]; // performance critical
			}
		}
		if (hoff != 0.0) { // add in low frequency terms
			for (int j = 0; j < xsize; ++j) {
				matrix[i][j] += hoff; // the earth "appears" flat when you're standing on it
			}
		}
	} // for i
}


float get_island_xy_element(int i, int dim, float val, float val2, float val3, int make_island) {

	float v(1.0);

	if (make_island == 2) {
		if (i < val2) {
			v = sinf(val3*i);
		}
		else if (i > (MESH_SIZE[dim] - val2)) {
			v = sinf(val3*(MESH_SIZE[dim]-i-1));
		}
	}
	else {
		v = cosf(get_dim_val(i, dim)*val);
	}
	if (ISLAND_SHAPE_EXP != 1.0) v = pow(fabs(v), ISLAND_SHAPE_EXP);
	return v;
}


void gen_mesh(int surface_type, int make_island, int keep_sin_table, int update_zvals) {

	float xf_scale((float)MESH_Y_SIZE/(float)MESH_X_SIZE), yf_scale(1.0/xf_scale);
	float mags[NUM_FREQ_COMP], freqs[NUM_FREQ_COMP];
	static bool init(0);
	vector<float> atten_table(MESH_X_SIZE);
	assert((read_landscape != 0) + (read_heightmap != 0) + (do_read_mesh != 0) <= 1);
	assert(surface_type != 2); // random+sin surface is no longer supported

	if (read_landscape) {
		surface_type = 3;
	}
	else if (read_heightmap) {
		surface_type = 4;
	}
	else if (do_read_mesh) {
		surface_type = 5;
	}
	bool surface_generated(0);
	bool const loaded_surface(surface_type >= 3);
	bool const gen_scroll_surface(GEN_SCROLLING_MESH && scrolling && loaded_surface);
	++cache_counter; // invalidate mesh cache
	int const num_freq(make_island ? NUM_L0_FREQ_COMP : NUM_FREQ_COMP);
	float const scaled_height(MESH_HEIGHT*mesh_height_scale);
	float const mesh_h((make_island ? ISLAND_MAG_SCALE : 1.0)*scaled_height/sqrt(0.1*N_RAND_SIN2));

	if (make_island) {
		mesh_scale   = 1.0;
		mesh_scale_z = 1.0;
	}
	compute_scale(make_island);
	island   = make_island;

	if (surface_type != 5) {
		zmax = -LARGE_ZVAL;
		zmin =  LARGE_ZVAL;
	}
	// Note: none of these config values are error checked, maybe should at least check >= 0.0
	freqs[0] = MESH_START_FREQ;
	mags [0] = MESH_START_MAG;

	for (int i = 1; i < num_freq; ++i) {
		freqs[i] = freqs[i-1]*MESH_FREQ_MULT;
		mags [i] = mags[i-1]*MESH_MAG_MULT;
	}
	if (make_island == 1) {
		mags[1] = 0.4;
		mags[2] = 0.2;
	}
	matrix_clear_2d(mesh_height);
	h_sum = 0.0;

	for (int l = 0; l < num_freq; ++l) {
		if (make_island == 1) {freqs[l] *= ISLAND_FREQ_SCALE;}
		h_sum += mags[l];
	}
	h_sum *= N_RAND_SIN2*scaled_height*HEIGHT_SCALE;

	if (surface_type == 0 || gen_scroll_surface) { // sine waves
		if (X_SCENE_SIZE > Y_SCENE_SIZE) yf_scale *= (float)Y_SCENE_SIZE/(float)X_SCENE_SIZE;
		if (Y_SCENE_SIZE > X_SCENE_SIZE) xf_scale *= (float)X_SCENE_SIZE/(float)Y_SCENE_SIZE;
		
		if (!keep_sin_table || !init) {
			if (mesh_seed != 0) set_rand2_state(mesh_seed, 12345);
			surface_generated = 1;
			zmm_calc = 0.0;

			for (int l = 0; l < num_freq; ++l) {
				int const offset(l*N_RAND_SIN2);
				float const x_freq(freqs[l]/((float)MESH_X_SIZE)), y_freq(freqs[l]/((float)MESH_Y_SIZE));
				float const mheight(mags[l]*mesh_h);

				for (int i = 0; i < N_RAND_SIN2; ++i) {
					int const index(offset + i);
					sinTable[index][0] = rand_uniform2(0.2, 1.0)*mheight; // magnitude
					sinTable[index][1] = rand_float2()*TWO_PI; // y phase
					sinTable[index][2] = rand_float2()*TWO_PI; // x phase
					sinTable[index][3] = rand_uniform2(0.01, 1.0)*x_freq*yf_scale; // y frequency
					sinTable[index][4] = rand_uniform2(0.01, 1.0)*y_freq*xf_scale; // x frequency
					zmm_calc += sinTable[index][0];
				}
			}
			zmm_calc /= mesh_scale_z;
			// *** sort sinTable? ***
		}
		if (world_mode == WMODE_GROUND || world_mode == WMODE_INF_TERRAIN) {
			gen_mesh_sine_table(mesh_height, xoff2, yoff2, MESH_X_SIZE, MESH_Y_SIZE, make_island);
		} // world_mode test
	} // end sine waves
	if (surface_type == 1) { // random
		zmax = -LARGE_ZVAL;
		zmin = -zmax;
		gen_mesh_random(0.1*scaled_height);
	}
	if (surface_type >= 3) { // read mesh image file
		static float **read_mh = NULL;

		if (scrolling) {
			assert(read_mh != NULL);

			if (abs(xoff2) < MESH_X_SIZE && abs(yoff2) < MESH_Y_SIZE) { // otherwise the entire read area is scrolled off the mesh
				float mh_scale(1.0);
				float const atten_dist(S_GEN_ATTEN_DIST*XY_SUM_SIZE/256);

				if (GEN_SCROLLING_MESH) { // still not entirely correct
					float mzmin, mzmax;
					matrix_min_max(mesh_height, mzmin, mzmax);
					mh_scale = CLIP_TO_01(zmax_est/max(fabs(mzmax), fabs(mzmin)));
				}
				for (int i = 0; i < MESH_Y_SIZE; ++i) {
					for (int j = 0; j < MESH_X_SIZE; ++j) { // scroll by absolute offset
						int xy[2] = {(j + xoff2), (i + yoff2)};
						int const xy0[2] = {xy[0], xy[1]};
						clamp_to_mesh(xy);
						float const mh(read_mh[xy[1]][xy[0]]);

						if (GEN_SCROLLING_MESH && (xy[0] != xy0[0] || xy[1] != xy0[1])) {
							float const atten(CLIP_TO_01((abs(xy[0] - xy0[0]) + abs(xy[1] - xy0[1]))/atten_dist));
							mesh_height[i][j] = atten*mh_scale*mesh_height[i][j] + (1.0 - atten)*mh; // not yet correct
						}
						else {
							mesh_height[i][j] = mh;
						}
					}
				}
			} // xoff/yoff test
		}
		else if (read_mh != NULL) {
			memcpy(mesh_height[0], read_mh[0], XY_MULT_SIZE*sizeof(float));
		}
		else {
			if (!((surface_type == 5) ? read_mesh(mesh_file, read_mesh_zmm) : read_mesh_height_image(mh_filename))) {
				cout << "Error reading landscape height data." << endl;
				exit(1);
			}
			if (read_mh == NULL) matrix_gen_2d(read_mh); // should this be deleted later? reread?
			memcpy(read_mh[0], mesh_height[0], XY_MULT_SIZE*sizeof(float));
		}
		//matrix_delete_2d(read_mh);
	} // end read
	update_disabled_mesh_height();
	if (surface_type != 5) {calc_zminmax();}

	if (make_island) {
		calc_zminmax();
		{
			float const val(SHAPE_PERIOD_SCALE*PI_TWO/X_SCENE_SIZE), val2(I_ATTEN_VAL*MESH_X_SIZE), val3(PI_TWO/val2);

			for (int i = 0; i < MESH_X_SIZE; ++i) {
				atten_table[i] = get_island_xy_element(i, 0, val, val2, val3, make_island);
			}
		}
		{
			float const val(SHAPE_PERIOD_SCALE*PI_TWO/Y_SCENE_SIZE), val2(I_ATTEN_VAL*MESH_Y_SIZE), val3(PI_TWO/val2);
			float const zmin2(zmin), zmin2b(zmin2 - 0.15);

			for (int i = 0; i < MESH_Y_SIZE; ++i) {
				float const cosY(get_island_xy_element(i, 1, val, val2, val3, make_island));
				
				for (int j = 0; j < MESH_X_SIZE; ++j) {
					float const zval((mesh_height[i][j] - zmin2b)*cosY*atten_table[j] + zmin2);
					zmin = min(zmin, zval);
					zmax = max(zmax, zval);
					mesh_height[i][j] = zval;
				}
			}
		}
		if (AUTOSCALE_HEIGHT && world_mode == WMODE_GROUND) {
			mesh_origin.z   = 0.0;
			camera_origin.z = 0.0;
		}
		water_plane_z = zmin - SMALL_NUMBER;
		set_zmax_est(max(zmax, -zmin));
		ztop          = zmax;
	} // end make_island
	else if (surface_type != 5) { // not make_island
		if (!keep_sin_table || !init || update_zvals) {
			if (AUTOSCALE_HEIGHT && world_mode == WMODE_GROUND) {
				float const zval(0.5*(zmin + zmax));
				mesh_origin.z   = zval;
				camera_origin.z = zval;
				surface_pos.z   = zval;
			}
			estimate_zminmax(surface_type < 3);
		}
		else {
			set_zvals();
		}
	} // end not make_island
	update_temperature(1);
	gen_terrain_map();

	if (GLACIATE && !make_island && world_mode == WMODE_GROUND && (!keep_sin_table || !init || update_zvals) && AUTOSCALE_HEIGHT) {
		float const zval(0.5*(zbottom + ztop)); // readjust camera height
		mesh_origin.z   = zval;
		camera_origin.z = zval;
		surface_pos.z   = zval;
	}
	//glaciate();
	if (surface_generated) init = 1;
}


float get_glaciated_zval(float zval) {
	float const relh((zval + zmax_est)*zmax_est2_inv);
	return DO_GLACIATE_EXP(relh)*zmax_est2 - zmax_est;
}

float calc_glaciated_rel_value(float value) {
	return zmin + DO_GLACIATE_EXP(value)*(zmax - zmin);
}

float do_glaciate_exp(float value) {
	return DO_GLACIATE_EXP(value);
}

float get_rel_wpz() {
	return CLIP_TO_01(W_PLANE_Z + water_h_off_rel);
}


void glaciate() {

	ztop             = -LARGE_ZVAL;
	zbottom          =  LARGE_ZVAL;
	glaciate_exp     = GLACIATE_EXP;
	glaciate_exp_inv = 1.0/glaciate_exp;

	for (int i = 0; i < MESH_Y_SIZE; ++i) {
		for (int j = 0; j < MESH_X_SIZE; ++j) {
			float const zval(get_glaciated_zval(mesh_height[i][j]));
			zbottom = min(zbottom, zval);
			ztop    = max(ztop, zval);
			mesh_height[i][j] = zval;
		}
	}
}


// should be named setup_lltex_sand_dirt() or something like that?
void init_terrain_mesh() {

	float const rel_wpz(get_rel_wpz());
	unsigned const sizes[2] = {NTEX_SAND, NTEX_DIRT}; // move into gen_tex_height_tables()?
	int const *const mesh_tids[2] = {mesh_tids_sand, mesh_tids_dirt};
	float const *const mesh_rh[2] = {mesh_rh_sand,   mesh_rh_dirt};
	ttex *ts[2] = {lttex_sand, lttex_dirt};

	for (unsigned d = 0; d < 2; ++d) {
		for (unsigned i = 0; i < sizes[d]; ++i) {
			ts[d][i].id = mesh_tids[d][i];
			float const def_h(mesh_rh[d][i]);
			float h;

			if (mesh_rh[d][i] < W_PLANE_Z) { // below water
				h = def_h*rel_wpz/W_PLANE_Z;
			}
			else { // above water
				float const rel_h((def_h - W_PLANE_Z)/(1.0 - W_PLANE_Z));
				h = rel_wpz + rel_h*(1.0 - rel_wpz);
				
				if (mesh_tids[d][i] == SNOW_TEX) {
					h = min(h, def_h); // snow can't get lower when water lowers
					if (temperature > 40.0) h += 0.01*(temperature - 40.0); // less snow with increasing temperature
				}
			}
			ts[d][i].zval = h;
		}
	}
	gen_tex_height_tables();
}


void gen_terrain_map() {

	if (GLACIATE && !island) {
		glaciate();
	}
	else {
		glaciate_exp     = 1.0;
		glaciate_exp_inv = 1.0;
	}
}


void estimate_zminmax(bool using_eq) {

	if (mesh_scale_change) {
		set_zmax_est(max(max(-zmin, zmax), zmax_est));
		mesh_scale_change = 0;
		set_zvals();
		return;
	}
	set_zmax_est(max(zmax, -zmin));

	if (using_eq && zmax == zmin) {
		set_zmax_est(zmax_est + 1.0E-6);
		return;
	}
	if (using_eq) {
		float const rm_scale(1000.0*XY_SCENE_SIZE/mesh_scale);
		mesh_xy_grid_cache_t height_gen;
		height_gen.build_arrays(0.0, 0.0, rm_scale, rm_scale, EST_RAND_PARAM, EST_RAND_PARAM);

		for (unsigned i = 0; i < EST_RAND_PARAM; ++i) {
			for (unsigned j = 0; j < EST_RAND_PARAM; ++j) {
				zmax_est = max(zmax_est, float(fabs(height_gen.eval_index(j, i, 0))));
			}
		}
	}
	set_zmax_est(1.1*zmax_est);
	set_zvals();
}


void set_zvals() {

	zcenter       = 0.5*(zmax + zmin);
	zbottom       = zmin;
	ztop          = zmax;
	zmin          = -zmax_est;
	zmax          =  zmax_est;
	water_plane_z = get_water_z_height();
	CLOUD_CEILING = CLOUD_CEILING0*Z_SCENE_SIZE;
	LARGE_ZVAL    = max(LARGE_ZVAL, 100.0f*(CLOUD_CEILING + ztop));
}


float get_water_z_height() {

	float wpz(get_rel_wpz());
	if (GLACIATE && !island) wpz = DO_GLACIATE_EXP(wpz);
	return wpz*zmax_est2 - zmax_est + water_h_off;
}


void update_temperature(bool verbose) {

	if (camera_mode != 1) return; // camera in air, don't use altitude temp

	// keep planet temperatures in combined landscape + universe
	alt_temp = (combined_gu ? univ_temp : init_temperature);

	if (island || read_landscape || read_heightmap || do_read_mesh) {
		temperature = alt_temp;
		return;
	}
	float const cur_z((camera_mode == 1) ? get_camera_pos().z : 0.5*(ztop + zbottom));
	float const rel_h((cur_z - zmin)/(zmax - zmin));
	//cout << "z: " << cur_z << ", rh: " << rel_h << ", wpz: " << water_plane_z << ", zmin: " << zmin << ", zmax: " << zmax << ", ztop: " << ztop << ", zbot: " << zbottom << endl;

	if (cur_z < water_plane_z) {
		float const znorm((water_plane_z - cur_z)/(water_plane_z - zmin));
		alt_temp *= (1.0 - 0.9*znorm); // underwater
	}
	else if (rel_h > 0.2) {
		alt_temp -= 60.0*(rel_h - 0.2); // in snow covered mountains
	}
	if (verbose /*&& temperature != alt_temp*/) cout << "Temperature = " << alt_temp << endl;
	temperature = alt_temp;
}


void compute_scale(int make_island) {

	int num_freq, iscale, filter;

	if (make_island) {
		num_freq = NUM_L0_FREQ_COMP;
		filter   = FREQ_FILTER + START_L0_FREQ_COMP - 1;
	}
	else {
		num_freq = NUM_FREQ_COMP;
		filter   = FREQ_FILTER;
	}
	iscale         = int(log(mesh_scale)/log(2.0));
	start_eval_sin = N_RAND_SIN2*max(0, min(num_freq-MIN_FREQS, (iscale+filter)));
	end_eval_sin   = N_RAND_SIN2*min(num_freq, (start_eval_sin+FREQ_RANGE));
}


void mesh_xy_grid_cache_t::build_arrays(float x0, float y0, float dx, float dy, unsigned nx, unsigned ny) {

	assert(nx >= 0 && ny >= 0);
	cur_nx = nx; cur_ny = ny;
	xterms.resize(nx*F_TABLE_SIZE);
	yterms.resize(ny*F_TABLE_SIZE);
	hoff = 0.0;
	float const ms_scale((island && world_mode == WMODE_GROUND) ? 1.0/ISLAND_MAG_SCALE : 1.0);
	float const mscale(ms_scale*mesh_scale), mscale_z(ms_scale*mesh_scale_z);
	float const msx(mscale*DX_VAL_INV), msy(mscale*DY_VAL_INV), ms2(0.5*mscale), msz_inv(1.0/mscale_z);

	if (end_eval_sin < F_TABLE_SIZE) {
		float const xval(msx*(x0 + 0.5*(nx-1)*dx)), yval(msy*(y0 + 0.5*(ny-1)*dy)); // center points

		for (int k = end_eval_sin; k < F_TABLE_SIZE; ++k) {
			hoff += sinTable[k][0]*SINF(sinTable[k][3]*yval + ms2*sinTable[k][3] + sinTable[k][1])
				                  *SINF(sinTable[k][4]*xval + ms2*sinTable[k][4] + sinTable[k][2]);
		}
		hoff *= msz_inv;
	}
	for (int k = start_eval_sin; k < end_eval_sin; ++k) {
		float const x_const(ms2*sinTable[k][4] + sinTable[k][2]), y_const(ms2*sinTable[k][3] + sinTable[k][1]);
		float const x_mult(msx*sinTable[k][4]), y_mult(msy*sinTable[k][3]), y_scale(msz_inv*sinTable[k][0]);

		for (unsigned i = 0; i < nx; ++i) {
			xterms[i*F_TABLE_SIZE+k] = SINF(x_mult*(x0 + i*dx) + x_const);
		}
		for (unsigned i = 0; i < ny; ++i) {
			yterms[i*F_TABLE_SIZE+k] = y_scale*SINF(y_mult*(y0 + i*dy) + y_const);
		}
	}
}


float mesh_xy_grid_cache_t::eval_index(unsigned x, unsigned y, bool glaciate) const {

	assert(x < cur_nx && y < cur_ny);
	float const *const xptr(&xterms.front() + x*F_TABLE_SIZE);
	float const *const yptr(&yterms.front() + y*F_TABLE_SIZE);
	float zval(hoff);
	for (int i = start_eval_sin; i < F_TABLE_SIZE; ++i) {zval += xptr[i]*yptr[i];} // performance critical
	if (GLACIATE && glaciate && !island) zval = get_glaciated_zval(zval);
	return zval;
}



float eval_mesh_sin_terms(float xv, float yv) {

	float zval(0.0);

	for (int k = start_eval_sin; k < F_TABLE_SIZE; ++k) { // could use end_eval_sin?
		float const *stk(sinTable[k]);
		zval += stk[0]*SINF(stk[3]*yv + stk[1])*SINF(stk[4]*xv + stk[2]); // performance critical
	}
	return zval;
}


float eval_one_surface_point(float xval, float yval) {

	if ((read_landscape || read_heightmap) && world_mode == WMODE_GROUND) { // interpolate from provided coords
		int xy[2] = {int(xval + 0.5), int(yval + 0.5)};
		clamp_to_mesh(xy);
		return mesh_height[xy[1]][xy[0]]; // could interpolate?
	}
	float const xv(mesh_scale*(xval + xoff2 - (MESH_X_SIZE >> 1))), yv(mesh_scale*(yval + yoff2 - (MESH_Y_SIZE >> 1)));
	float const zval(eval_mesh_sin_terms(xv, yv)/mesh_scale_z);
	return ((GLACIATE && !island) ? get_glaciated_zval(zval) : zval);
}


void reset_offsets() {
	
	if (world_mode == WMODE_INF_TERRAIN) { // terrain
		point camera(get_camera_pos());
		int const ssize((int)pow(RES_STEP, resolution-1));
		xoff2 += int(STEP_SIZE*ssize*(get_xpos(camera.x) - MESH_X_SIZE/2));
		yoff2 += int(STEP_SIZE*ssize*(get_ypos(camera.y) - MESH_Y_SIZE/2));
	}
	else { // normal
		surface_pos = all_zeros;
	}
	xoff = yoff = 0;
}


void update_mesh(float dms, bool do_regen_trees) { // called when mesh_scale changes

	++cache_counter;
	mesh_scale /= dms;
	tree_scale  = mesh_scale;
	xoff2       = int(xoff2*dms);
	yoff2       = int(yoff2*dms);
	set_zmax_est(zmax_est*pow(dms, (float)MESH_SCALE_Z_EXP));
	mesh_scale_z = pow(mesh_scale, (float)MESH_SCALE_Z_EXP);

	if (world_mode == WMODE_INF_TERRAIN) {
		zmax = -LARGE_ZVAL;
		zmin =  LARGE_ZVAL;
		compute_scale(0);
		estimate_zminmax(1);
		gen_tex_height_tables();
	}
	else {
		gen_scene(1, do_regen_trees, 1, 1, 0);
		regen_lightmap();
	}
}


bool is_under_mesh(point const &p) {

	return (p.z < zbottom || p.z < interpolate_mesh_zval(p.x, p.y, 0.0, 0, 1));
}


bool read_mesh(const char *filename, float zmm) {

	if (filename == NULL) {
		cout << "No input mesh file, using generated mesh." << endl;
		return 0;
	}
	FILE *fp;
	if (!open_file(fp, filename, "input mesh")) return 0;
	int xsize, ysize;
	float height;

	if (fscanf(fp, "%u%u", &xsize, &ysize) != 2) {
		cout << "Error reading size header in input file '" << filename << "'." << endl;
		fclose(fp);
		return 0;
	}
	if (xsize != MESH_X_SIZE || ysize != MESH_Y_SIZE) {
		cout << "Error: Mesh size in file is " << xsize << "x" << ysize << " but should be " << MESH_X_SIZE << "x" << MESH_Y_SIZE << "." << endl;
		fclose(fp);
		return 0;
	}
	for (int i = 0; i < MESH_Y_SIZE; ++i) {
		for (int j = 0; j < MESH_X_SIZE; ++j) {
			if (fscanf(fp, "%f", &height) != 1) {
				cout << "Error reading mesh heights from file at position (" << j << ", " << i << ")." << endl;
				fclose(fp);
				return 0;
			}
			mesh_height[i][j] = mesh_file_scale*height + mesh_file_tz;
		}
	}
	fclose(fp);
	update_disabled_mesh_height();
	calc_zminmax();
	set_zmax_est((zmm != 0.0) ? zmm : max(-zmin, zmax));
	set_zvals();
	cout << "Read input mesh file '" << filename << "'." << endl;
	return 1;
}


bool write_mesh(const char *filename) {

	if (mesh_height == NULL) return 0;

	if (filename == NULL) {
		cout << "Error in filename: Cannot write mesh." << endl;
		return 0;
	}
	FILE *fp;
	if (!open_file(fp, filename, "mesh output", "w")) return 0;

	if (!fprintf(fp, "%u %u\n", MESH_X_SIZE, MESH_Y_SIZE)) {
		cout << "Error writing size header for mesh file '" << filename << "'." << endl;
		fclose(fp);
		return 0;
	}
	for (int i = 0; i < MESH_Y_SIZE; ++i) {
		for (int j = 0; j < MESH_X_SIZE; ++j) {
			if (!fprintf(fp, "%f ", mesh_height[i][j])) {
				cout << "Error writing mesh heights to file at position " << j << ", " << i << endl;
				fclose(fp);
				return 0;
			}
		}
		fprintf(fp, "\n");
	}
	fclose(fp);
	cout << "Wrote mesh file '" << filename << "'." << endl;
	return 1;
}


bool load_state(const char *filename) {

	if (filename == NULL) {
		cout << "Error in filename: Cannot load state." << endl;
		return 0;
	}
	FILE *fp;
	if (!open_file(fp, filename, "input state")) return 0;
	int v1, v2, v3, v4;
	++cache_counter;

	if (fscanf(fp, "%lf%lf%lf%f%f%f%f%f%f%i%i%i%i%i%li%li%u%u%u%u", &c_radius, &c_phi, &c_theta, &camera_origin.x,
		&camera_origin.y, &camera_origin.z, &surface_pos.x, &surface_pos.y, &surface_pos.z, &xoff, &yoff, &xoff2, &yoff2,
		&rand_gen_index, &global_rand_gen.rseed1, &global_rand_gen.rseed2, &v1, &v2, &v3, &v4) != 20)
	{
		cout << "Error reading state header." << endl;
		fclose(fp);
		return 0;
	}
	if (v1 != MESH_X_SIZE || v2 != MESH_Y_SIZE || v3 != NUM_FREQ_COMP || v4 != N_RAND_SIN2) {
		cout << "Error: Saved state is incompatible with current configuration." << endl;
		fclose(fp);
		return 0;
	}
	for (int i = 0; i < F_TABLE_SIZE; ++i) {
		for (int k = 0; k < 5; ++k) {
			if (fscanf(fp, "%f ", &(sinTable[i][k])) != 1) {
				cout << "Error reading state table entry (" << i << ", " << k << ")." << endl;
				fclose(fp);
				return 0;
			}
		}
	}
	fclose(fp);
	update_cpos();
	if (world_mode == WMODE_GROUND) {gen_scene(1, (world_mode == WMODE_GROUND), 1, 1, 0);}
	cout << "State file '" << filename << "' has been loaded." << endl;
	return 1;
}


bool save_state(const char *filename) {

	if (filename == NULL) {
		cout << "Error in filename: Cannot save state." << endl;
		return 0;
	}
	FILE *fp;
	if (!open_file(fp, filename, "output state", "w")) return 0;

	if (!fprintf(fp, "%lf %lf %lf %f %f %f %f %f %f %i %i %i %i %i %li %li\n%u %u %u %u\n",
		c_radius, c_phi, c_theta, camera_origin.x, camera_origin.y, camera_origin.z, surface_pos.x, surface_pos.y, surface_pos.z,
		xoff, yoff, xoff2, yoff2, rand_gen_index, global_rand_gen.rseed1, global_rand_gen.rseed2, MESH_X_SIZE, MESH_Y_SIZE, NUM_FREQ_COMP, N_RAND_SIN2))
	{
		cout << "Error writing state header." << endl;
		fclose(fp);
		return 0;
	}
	for (unsigned i = 0; i < (unsigned)F_TABLE_SIZE; ++i) {
		for (unsigned k = 0; k < 5; ++k) {
			if (!fprintf(fp, "%f ", sinTable[i][k])) {
				cout << "Error writing state table entry (" << i << ", " << k << ")." << endl;
				fclose(fp);
				return 0;
			}
		}
		fprintf(fp, "\n");
	}
	fclose(fp);
	cout << "State file '" << filename << "' has been saved." << endl;
	return 1;
}


