// 3D World - City Generation
// by Frank Gennari
// 2/10/18

#include "3DWorld.h"
#include "mesh.h"
#include "heightmap.h"
#include "file_utils.h"
#include "draw_utils.h"
#include "shaders.h"

using std::string;

bool const CHECK_HEIGHT_BORDER_ONLY = 1; // choose building site to minimize edge discontinuity rather than amount of land that needs to be modified


extern int rand_gen_index, display_mode;
extern float water_plane_z;


class city_plot_gen_t {

protected:
	struct rect_t {
		unsigned x1, y1, x2, y2;
		rect_t() : x1(0), y1(0), x2(0), y2(0) {}
		rect_t(unsigned x1_, unsigned y1_, unsigned x2_, unsigned y2_) : x1(x1_), y1(y1_), x2(x2_), y2(y2_) {}
		bool is_valid() const {return (x1 < x2 && y1 < y2);}
		unsigned get_area() const {return (x2 - x1)*(y2 - y1);}
		bool operator== (rect_t const &r) const {return (x1 == r.x1 && y1 == r.y1 && x2 == r.x2 && y2 == r.y2);}
		bool has_overlap(rect_t const &r) const {return (x1 < r.x2 && y1 < r.y2 && r.x1 < x2 && r.y1 < y2);}
	};

	float *heightmap;
	unsigned xsize, ysize;
	int last_rgi;
	rand_gen_t rgen;
	vector<rect_t> used;
	vector<cube_t> plots; // same size as used

	bool is_valid_region(unsigned x1, unsigned y1, unsigned x2, unsigned y2) const {
		return (x1 < x2 && y1 < y2 && x2 <= xsize && y2 <= ysize);
	}
	bool overlaps_used(unsigned x1, unsigned y1, unsigned x2, unsigned y2) const {
		rect_t const cur(x1, y1, x2, y2);
		for (vector<rect_t>::const_iterator i = used.begin(); i != used.end(); ++i) {if (i->has_overlap(cur)) return 1;} // simple linear iteration
		return 0;
	}
	cube_t add_plot(unsigned x1, unsigned y1, unsigned x2, unsigned y2, float elevation) {
		cube_t bcube;
		int const dx(-int(xsize)/2), dy(-int(ysize)/2); // convert from center to LLC
		bcube.d[0][0] = get_xval(x1 + dx);
		bcube.d[0][1] = get_xval(x2 + dx);
		bcube.d[1][0] = get_yval(y1 + dy);
		bcube.d[1][1] = get_yval(y2 + dy);
		bcube.d[2][0] = bcube.d[2][1] = elevation;
		plots.push_back(bcube);
		used.emplace_back(x1, y1, x2, y2);
		return bcube;
	}
	float any_underwater(unsigned x1, unsigned y1, unsigned x2, unsigned y2) const {
		assert(is_valid_region(x1, y1, x2, y2));

		for (unsigned y = y1; y < y2; ++y) {
			for (unsigned x = x1; x < x2; ++x) {
				if (CHECK_HEIGHT_BORDER_ONLY && y != y1 && y != y2-1 && x == x1+1) {x = x2-1;} // jump to right edge
				if (heightmap[y*xsize + x] < water_plane_z) return 1;
			}
		}
		return 0;
	}
	float get_avg_height(unsigned x1, unsigned y1, unsigned x2, unsigned y2) const {
		assert(is_valid_region(x1, y1, x2, y2));
		float sum(0.0), denom(0.0);

		for (unsigned y = y1; y < y2; ++y) {
			for (unsigned x = x1; x < x2; ++x) {
				if (CHECK_HEIGHT_BORDER_ONLY && y != y1 && y != y2-1 && x == x1+1) {x = x2-1;} // jump to right edge
				sum   += heightmap[y*xsize + x];
				denom += 1.0;
			}
		}
		return sum/denom;
	}
	float get_rms_height_diff(unsigned x1, unsigned y1, unsigned x2, unsigned y2) const {
		float const avg(get_avg_height(x1, y1, x2, y2));
		float diff(0.0);

		for (unsigned y = y1; y < y2; ++y) {
			for (unsigned x = x1; x < x2; ++x) {
				if (CHECK_HEIGHT_BORDER_ONLY && y != y1 && y != y2-1 && x == x1+1) {x = x2-1;} // jump to right edge
				float const delta(heightmap[y*xsize + x] - avg);
				diff += delta*delta; // square the difference
			}
		}
		return diff;
	}
	vector3d const get_query_xlate() const {
		return vector3d((world_mode == WMODE_INF_TERRAIN) ? vector3d((xoff - xoff2)*DX_VAL, (yoff - yoff2)*DY_VAL, 0.0) : zero_vector);
	}
public:
	city_plot_gen_t() : heightmap(nullptr), xsize(0), ysize(0), last_rgi(0) {}

	void init(float *heightmap_, unsigned xsize_, unsigned ysize_) {
		heightmap = heightmap_; xsize = xsize_; ysize = ysize_;
		assert(heightmap != nullptr);
		assert(xsize > 0 && ysize > 0); // any size is okay
		if (rand_gen_index != last_rgi) {rgen.set_state(rand_gen_index, 12345); last_rgi = rand_gen_index;} // only when rand_gen_index changes
	}
	bool find_best_city_location(unsigned width, unsigned height, unsigned border, unsigned num_samples, unsigned &x_llc, unsigned &y_llc) {
		cout << TXT(xsize) << TXT(ysize) << TXT(width) << TXT(height) << TXT(border) << endl;
		assert(num_samples > 0);
		assert((width + 2*border) < xsize && (height + 2*border) < ysize); // otherwise the city can't fit in the map
		unsigned const num_iters(100*num_samples); // upper bound
		unsigned xend(xsize - width - 2*border + 1), yend(ysize - width - 2*border + 1); // max rect LLC, inclusive
		unsigned num_cands(0);
		float best_diff(0.0);

		for (unsigned n = 0; n < num_iters; ++n) { // find min RMS height change across N samples
			unsigned const x1(border + (rgen.rand()%xend)), y1(border + (rgen.rand()%yend));
			unsigned const x2(x1 + width), y2(y1 + height);
			if (overlaps_used (x1, y1, x2, y2)) continue; // skip
			if (any_underwater(x1, y1, x2, y2)) continue; // skip
			float const diff(get_rms_height_diff(x1, y1, x2, y2));
			if (num_cands == 0 || diff < best_diff) {x_llc = x1; y_llc = y1; best_diff = diff;}
			if (++num_cands == num_samples) break; // done
		} // for n
		if (num_cands == 0) return 0;
		cout << "cands: " << num_cands << ", diff: " << best_diff << ", loc: " << x_llc << "," << y_llc << endl;
		return 1; // success
	}
	float flatten_region(unsigned x1, unsigned y1, unsigned x2, unsigned y2, unsigned slope_width, float const *const height=nullptr) {
		assert(is_valid_region(x1, y1, x2, y2));
		float const delta_h = 0.0; // for debugging in map view
		float const elevation(height ? *height : (get_avg_height(x1, y1, x2, y2) + delta_h));

		for (unsigned y = max((int)y1-(int)slope_width, 0); y < min(y2+slope_width, ysize); ++y) {
			for (unsigned x = max((int)x1-(int)slope_width, 0); x < min(x2+slope_width, xsize); ++x) {
				float const dx(max(0, max(((int)x1 - (int)x), ((int)x - (int)x2 + 1))));
				float const dy(max(0, max(((int)y1 - (int)y), ((int)y - (int)y2 + 1))));
				float const mix(sqrt(dx*dx + dy*dy)/slope_width);
				float &h(heightmap[y*xsize + x]);
				h = mix*h + (1.0 - mix)*elevation;
			}
		}
		return elevation;
	}
	bool check_plot_sphere_coll(point const &pos, float radius, bool xy_only=1) const {
		if (plots.empty()) return 0;
		point const sc(pos - get_query_xlate());

		for (auto i = plots.begin(); i != plots.end(); ++i) {
			if (xy_only ? sphere_cube_intersect_xy(sc, radius, *i) : sphere_cube_intersect(sc, radius, *i)) return 1;
		}
		return 0;
	}
}; // city_plot_gen_t


class city_road_gen_t {

	struct road_t {
		point start, end;
		vector3d dw;
		road_t() {}
		road_t(point const &s, point const &e, float width) : start(s), end(e) {
			assert(start != end);
			assert(width > 0.0);
			dw = 0.5*width*cross_product((end - start), plus_z).get_norm();
		}
		cube_t get_bcube() const {
			point const pts[4] = {(start - dw), (start + dw), (end + dw), (end - dw)};
			return cube_t(pts, 4);
		}
		void draw(quad_batch_draw &qbd) const {
			point const pts[4] = {(start - dw), (start + dw), (end + dw), (end - dw)};
			qbd.add_quad_pts(pts, colorRGBA(0.2, 0.2, 0.2, 1.0), plus_z, tex_range_t()); // dark gray, normal in +z
		}
	};

	struct road_network_t : public vector<road_t> {
		cube_t bcube;

		road_network_t(cube_t const &bcube_) : bcube(bcube_) {
			bcube.d[2][1] += SMALL_NUMBER; // make it nonzero size
		}
		void get_bcubes(vector<cube_t> &bcubes) const {
			for (const_iterator r = begin(); r != end(); ++r) {bcubes.push_back(r->get_bcube());}
		}
		void draw(quad_batch_draw &qbd, vector3d const &xlate) const {
			if (!camera_pdu.cube_visible(bcube + xlate)) return; // VFC
			for (const_iterator r = begin(); r != end(); ++r) {r->draw(qbd);}
		}
	};

	vector<road_network_t> road_networks;
	quad_batch_draw qbd;

public:
	void gen_roads(cube_t const &region, float road_width, float road_spacing) {
		timer_t timer("Gen Roads");
		vector3d const size(region.get_size());
		assert(size.x > 0.0 && size.y > 0.0);
		float const half_width(0.5*road_width), road_pitch(road_width + road_spacing);
		float const zval(region.d[2][0] + SMALL_NUMBER);
		road_networks.push_back(road_network_t(region));
		road_network_t &roads(road_networks.back());
		
		// create a grid, for now; crossing roads will overlap
		// FIXME: add proper intersections later
		for (float x = region.d[0][0]+half_width; x < region.d[0][1]-half_width; x += road_pitch) { // shrink to include centerlines
			roads.emplace_back(point(x, region.d[1][0], zval), point(x, region.d[1][1], zval), road_width);
		}
		for (float y = region.d[1][0]+half_width; y < region.d[1][1]-half_width; y += road_pitch) { // shrink to include centerlines
			roads.emplace_back(point(region.d[0][0], y, zval), point(region.d[0][1], y, zval), road_width);
		}
		cout << "Roads: " << roads.size() << endl;
	}
	void get_all_bcubes(vector<cube_t> &bcubes) const {
		for (auto r = road_networks.begin(); r != road_networks.end(); ++r) {r->get_bcubes(bcubes);}
	}
	void draw(vector3d const &xlate) { // non-const because qbd is modified
		shader_t s;
		s.begin_color_only_shader(); // FIXME: textured?
		fgPushMatrix();
		translate_to(xlate);
		glDepthFunc(GL_LEQUAL); // helps prevent Z-fighting
		for (auto r = road_networks.begin(); r != road_networks.end(); ++r) {r->draw(qbd, xlate);}
		qbd.draw_and_clear();
		glDepthFunc(GL_LESS);
		s.end_shader();
		fgPopMatrix();
	}
}; // city_road_gen_t


struct city_params_t {

	unsigned num_cities, num_samples, city_size, city_border, slope_width;
	float road_width, road_spacing;

	city_params_t() : num_cities(0), num_samples(100), city_size(0), city_border(0), slope_width(0), road_width(0.0), road_spacing(0.0) {}
	bool enabled() const {return (num_cities > 0 && city_size > 0);}
	static bool read_error(string const &str) {cout << "Error reading city config option " << str << "." << endl; return 0;}

	bool read_option(FILE *fp) {
		char strc[MAX_CHARS] = {0};
		if (!read_str(fp, strc)) return 0;
		string const str(strc);

		if (str == "num_cities") {
			if (!read_uint(fp, num_cities)) {return read_error(str);}
		}
		else if (str == "num_samples") {
			if (!read_uint(fp, num_samples) || num_samples == 0) {return read_error(str);}
		}
		else if (str == "city_size") {
			if (!read_uint(fp, city_size)) {return read_error(str);}
		}
		else if (str == "city_border") {
			if (!read_uint(fp, city_border)) {return read_error(str);}
		}
		else if (str == "slope_width") {
			if (!read_uint(fp, slope_width)) {return read_error(str);}
		}
		else if (str == "road_width") {
			if (!read_float(fp, road_width) || road_width < 0.0) {return read_error(str);}
		}
		else if (str == "road_spacing") {
			if (!read_float(fp, road_spacing) || road_spacing < 0.0) {return read_error(str);}
		}
		else {
			cout << "Unrecognized city keyword in input file: " << str << endl;
			return 0;
		}
		return 1;
	}
}; // city_params_t


class city_gen_t : public city_plot_gen_t {

	city_road_gen_t road_gen;

public:
	bool gen_city(city_params_t const &params) {
		timer_t t("Choose City Location");
		unsigned x1(0), y1(0);
		if (!find_best_city_location(params.city_size, params.city_size, params.city_border, params.num_samples, x1, y1)) return 0;
		unsigned const x2(x1 + params.city_size), y2(y1 + params.city_size);
		float const elevation(flatten_region(x1, y1, x2, y2, params.slope_width));
		cube_t const pos_range(add_plot(x1, y1, x2, y2, elevation));
		set_buildings_pos_range(pos_range);
		if (params.road_width > 0.0 && params.road_spacing > 0.0) {road_gen.gen_roads(pos_range, params.road_width, params.road_spacing);}
		return 1;
	}
	void gen_cities(city_params_t const &params) {
		for (unsigned n = 0; n < params.num_cities; ++n) {gen_city(params);}
	}
	void get_all_road_bcubes(vector<cube_t> &bcubes) const {road_gen.get_all_bcubes(bcubes);}

	void draw(bool shadow_only, vector3d const &xlate) { // for now, there are only roads
		if (!shadow_only) {road_gen.draw(xlate);} // roads don't cast shadows
		// buildings are drawn through draw_buildings()
	}
}; // city_gen_t


city_params_t city_params;
city_gen_t city_gen;


bool parse_city_option(FILE *fp) {return city_params.read_option(fp);}
bool have_cities() {return city_params.enabled();}

void gen_cities(float *heightmap, unsigned xsize, unsigned ysize) {
	if (!have_cities()) return; // nothing to do
	city_gen.init(heightmap, xsize, ysize); // only need to call once for any given heightmap
	city_gen.gen_cities(city_params);
}
void get_city_road_bcubes(vector<cube_t> &bcubes) {city_gen.get_all_road_bcubes(bcubes);}
void draw_cities(bool shadow_only, vector3d const &xlate) {city_gen.draw(shadow_only, xlate);}

bool check_city_sphere_coll(point const &pos, float radius) {
	if (!have_cities()) return 0;
	point center(pos);
	if (world_mode == WMODE_INF_TERRAIN) {center += vector3d(xoff*DX_VAL, yoff*DY_VAL, 0.0);} // apply xlate for all static objects
	return city_gen.check_plot_sphere_coll(center, radius);
}
bool check_valid_scenery_pos(point const &pos, float radius) {
	if (check_buildings_sphere_coll(pos, radius, 1, 1)) return 0; // apply_tt_xlate=1, xy_only=1
	if (check_city_sphere_coll(pos, radius)) return 0;
	return 1;
}

