// 3D World - Building/House Attic Logic
// by Frank Gennari 06/10/2022

#include "3DWorld.h"
#include "function_registry.h"
#include "buildings.h"

colorRGBA get_light_color_temp(float t);
unsigned get_face_mask(unsigned dim, bool dir);
unsigned get_skip_mask_for_xy(bool dim);
colorRGBA apply_light_color(room_object_t const &o, colorRGBA const &c);


bool building_t::point_in_attic(point const &pos, vector3d *const cnorm) const {
	if (!has_attic() || pos.z < interior->attic_access.z1() || pos.z > interior_z2) return 0;

	// check if pos is under the roof
	for (auto const &tq : roof_tquads) {
		if (tq.type != tquad_with_ix_t::TYPE_ROOF) continue;
		if (!point_in_polygon_2d(pos.x, pos.y, tq.pts, tq.npts, 0, 1)) continue; // check 2D XY point containment
		vector3d const normal(tq.get_norm());
		if (normal.z == 0.0) continue; // skip vertical sides
		if (cnorm) {*cnorm = -normal;} // we're looking at the underside of the roof, so reverse the normal; set whether or not we're inside the attic
		if (dot_product_ptv(normal, pos, tq.pts[0]) < 0.0) return 1;
	}
	return 0;
}

void building_t::add_attic_objects(rand_gen_t rgen) {
	vect_room_object_t &objs(interior->room_geom->objs);
	// add attic access door
	cube_with_ix_t adoor(interior->attic_access);
	assert(adoor.is_strictly_normalized());
	adoor.expand_in_dim(2, -0.2*adoor.dz()); // shrink in z
	int const room_id(get_room_containing_pt(point(adoor.xc(), adoor.yc(), adoor.z1()-get_floor_thickness()))); // should we cache this during floorplanning?
	assert(room_id >= 0); // must be found
	room_t const &room(get_room(room_id));
	bool const dim(adoor.ix >> 1), dir(adoor.ix & 1);
	// Note: not setting RO_FLAG_NOCOLL because we do want to collide with this when open
	unsigned const acc_flags(room.is_hallway ? RO_FLAG_IN_HALLWAY : 0);
	// is light_amount=1.0 correct? since this door can be viewed from both inside and outside the attic, a single number doesn't really work anyway
	objs.emplace_back(adoor, TYPE_ATTIC_DOOR, room_id, dim, dir, acc_flags, 1.0, SHAPE_CUBE); // Note: player collides with open attic door

	// add light
	float const attic_height(interior_z2 - adoor.z2()), light_radius(0.06*attic_height);
	cube_t const part(get_part_for_room(room)); // Note: assumes attic is a single part
	point const light_center(part.xc(), part.yc(), (interior_z2 - 1.33*light_radius)); // center of the part near the ceiling
	cube_t light; light.set_from_sphere(light_center, light_radius);
	// start off lit for now; maybe should start off and auto turn on when the player enters the attic?
	unsigned const light_flags(RO_FLAG_LIT | RO_FLAG_EMISSIVE | RO_FLAG_NOCOLL | RO_FLAG_INTERIOR | RO_FLAG_IN_ATTIC);
	objs.emplace_back(light, TYPE_LIGHT, room_id, 0, 0, light_flags, 1.0, SHAPE_SPHERE, get_light_color_temp(0.45)); // yellow-shite
}

cube_t get_attic_access_door_cube(room_object_t const &c) {
	if (!c.is_open()) return c;
	float const len(c.get_sz_dim(c.dim)), thickness(c.dz()), delta(len - thickness);
	cube_t door(c);
	door.z1() -= delta; // open downward
	door.d[c.dim][!c.dir] -= (c.dir ? -1.0 : 1.0)*delta; // shorten to expose the opening
	return door;
}
cube_t get_ladder_bcube_from_open_attic_door(room_object_t const &c, cube_t const &door) {
	float const door_len(c.get_sz_dim(c.dim)), door_width(c.get_sz_dim(!c.dim)), door_inside_edge(door.d[c.dim][!c.dir]);
	cube_t ladder(door); // sets ladder step depth
	ladder.expand_in_dim(!c.dim, -0.05*door_width); // a bit narrower
	ladder.d[c.dim][ c.dir] = door_inside_edge; // flush with open side of door
	ladder.d[c.dim][!c.dir] = door_inside_edge + (c.dir ? -1.0 : 1.0)*2.0*c.dz();
	ladder.z1() = door.z2() - 0.95*(door_len/0.44); // matches door length calculation used in floorplanning step
	return ladder;
}
void building_room_geom_t::add_attic_door(room_object_t const &c, float tscale) {
	rgeom_mat_t &wood_mat(get_wood_material(tscale, 1, 0, 1)); // shadows + small
	colorRGBA const color(apply_light_color(c, c.color));

	if (c.is_open()) {
		unsigned const qv_start1(wood_mat.quad_verts.size());
		cube_t const door(get_attic_access_door_cube(c));
		wood_mat.add_cube_to_verts(door, color, door.get_llc(), 0); // all sides
		// rotate 10 degrees
		point rot_pt;
		rot_pt[ c.dim] = door.d[c.dim][!c.dir]; // door inside edge
		rot_pt[!c.dim] = c.get_center_dim(!c.dim); // doesn't matter?
		rot_pt.z       = door.z2(); // top of door
		vector3d const rot_axis(c.dim ? -plus_x : plus_y);
		float const rot_angle((c.dir ? -1.0 : 1.0)*10.0*TO_RADIANS);
		rotate_verts(wood_mat.quad_verts, rot_axis, rot_angle, rot_pt, qv_start1);
		// draw the ladder
		colorRGBA const ladder_color(apply_light_color(c, LT_BROWN)); // slightly darker
		rgeom_mat_t &ladder_mat(get_wood_material(2.0*tscale, 1, 0, 1)); // shadows + small; larger tscale
		unsigned const qv_start2(ladder_mat.quad_verts.size());
		cube_t const ladder(get_ladder_bcube_from_open_attic_door(c, door));
		float const ladder_width(ladder.get_sz_dim(!c.dim));
		float const side_width_factor = 0.05; // relative to door_width

		for (unsigned n = 0; n < 2; ++n) { // sides
			cube_t side(ladder);
			side.d[!c.dim][!n] -= (n ? -1.0 : 1.0)*(1.0 - side_width_factor)*ladder_width;
			ladder_mat.add_cube_to_verts(side, ladder_color, side.get_llc(), EF_Z1, 1); // skip bottom, swap_tex_st=1
		}
		// draw the steps
		unsigned const num_steps = 10;
		float const step_spacing(ladder.dz()/(num_steps+1)), step_thickness(0.1*step_spacing);
		cube_t step(ladder);
		step.expand_in_dim(!c.dim, -side_width_factor*ladder_width);

		for (unsigned n = 0; n < num_steps; ++n) { // steps
			step.z1() = ladder.z1() + (n+1)*step_spacing;
			step.z2() = step  .z1() + step_thickness;
			ladder_mat.add_cube_to_verts(step, ladder_color, step.get_llc(), get_skip_mask_for_xy(!c.dim), 1); // skip sides, swap_tex_st=1
		}
		rotate_verts(ladder_mat.quad_verts, rot_axis, rot_angle, rot_pt, qv_start2);
	}
	else { // draw only the top and bottom faces of the door
		wood_mat.add_cube_to_verts(c, color, c.get_llc(), ~EF_Z12); // shadows + small, top and bottom only
	}
}

struct edge_t {
	point p[2];
	edge_t() {}
	edge_t(point const &A, point const &B, bool cmp_dim) {
		p[0] = A; p[1] = B;
		if (B[cmp_dim] < A[cmp_dim]) {swap(p[0], p[1]);} // make A less in cmp_dim
	}
};

void building_room_geom_t::add_attic_woodwork(building_t const &b, float tscale) {
	if (!b.has_attic()) return;
	rgeom_mat_t &wood_mat(get_wood_material(tscale, 1, 0, 2)); // shadows + detail
	float const attic_z1(b.interior->attic_access.z1()), delta_z(0.1*b.get_floor_thickness()); // matches value in get_all_drawn_verts()
	float const floor_spacing(b.get_window_vspace());

	// Note: there may be a chimney in the attic, but for now we ignore it
	for (auto i = b.roof_tquads.begin(); i != b.roof_tquads.end(); ++i) {
		if (i->get_bcube().z1() < attic_z1) continue; // not the top section that has the attic (porch roof, lower floor roof)
		bool const is_roof(i->type == tquad_with_ix_t::TYPE_ROOF); // roof tquad
		bool const is_wall(i->type == tquad_with_ix_t::TYPE_WALL); // triangular exterior wall section; brick or block, doesn't need wood support, but okay to add
		if (!is_roof && !is_wall) continue;
		// draw beams along inside of roof; start with a vertical cube and rotate to match roof angle
		tquad_with_ix_t tq(*i);
		for (unsigned n = 0; n < tq.npts; ++n) {tq.pts[n].z -= delta_z;} // shift down slightly
		cube_t const bcube(tq.get_bcube());
		vector3d const normal(tq.get_norm()); // points outside of the attic
		bool const dim(fabs(normal.x) < fabs(normal.y)), dir(normal[dim] > 0); // dim this tquad is facing; beams run in the other dim
		float const base_width(bcube.get_sz_dim(!dim)), run_len(bcube.get_sz_dim(dim)), height(bcube.dz()), height_scale(1.0/fabs(normal[dim]));
		float const beam_width(0.04*floor_spacing), beam_hwidth(0.5*beam_width), beam_depth(2.0*beam_width);
		float const epsilon(0.02*beam_hwidth), beam_edge_gap(beam_hwidth + epsilon), dir_sign(dir ? -1.0 : 1.0);
		unsigned const num_beams(max(2, round_fp(3.0f*base_width/floor_spacing)));
		float const beam_spacing((base_width - 2.0f*beam_edge_gap)/(num_beams - 1));
		// shift slightly for opposing roof sides to prevent Z-fighting on center beam
		float const beam_pos_start(bcube.d[!dim][0] + beam_edge_gap + dir_sign*0.5*epsilon);
		unsigned const qv_start(wood_mat.quad_verts.size());
		cube_t beam(bcube); // set the z1 base and exterior edge d[dim][dir]
		if (is_roof) {beam.z1() += beam_depth*run_len/height;} // shift up to avoid clipping through the ceiling of the room below
		// determine segments for our non-base edges
		edge_t edges[3]; // non-base edge segments: start plus: 1 for rectangle, 2 for triangle, 3 for trapezoid
		unsigned num_edges(0);

		for (unsigned n = 0; n < tq.npts; ++n) {
			point const &A(tq.pts[n]), &B(tq.pts[(n+1)%tq.npts]);
			if (A.z == bcube.z1() && B.z == bcube.z1()) continue; // base edge, skip
			if (A[!dim] == B[!dim]) continue; // non-angled edge, skip
			edges[num_edges++] = edge_t(A, B, !dim);
		}
		assert(num_edges > 0 && num_edges <= 3);

		// add vertical beams
		for (unsigned n = 0; n < num_beams; ++n) {
			float const roof_pos(beam_pos_start + n*beam_spacing);
			set_wall_width(beam, roof_pos, beam_hwidth, !dim);
			beam.d[dim][!dir] = beam.d[dim][dir] + dir_sign*beam_depth;
			bool found(0);

			for (unsigned e = 0; e < num_edges; ++e) {
				edge_t const &E(edges[e]);
				if (roof_pos < E.p[0][!dim] || roof_pos >= E.p[1][!dim]) continue; // beam not contained in this edge
				if (E.p[0].z == E.p[1].z) {beam.z2() = E.p[0].z;} // horizontal edge
				else {beam.z2() = E.p[0].z + ((roof_pos - E.p[0][!dim])/(E.p[1][!dim] - E.p[0][!dim]))*(E.p[1].z - E.p[0].z);} // interpolate zval
				beam.z2() += (height_scale - 1.0)*(beam.z2() - bcube.z1()); // rescale to account for length post-rotate
				if (is_wall) {beam.z2() -= beam_hwidth*height/(0.5*base_width);} // shorten to avoid clipping through the roof at the top
				assert(!found); // break instead?
				found = 1;
			} // for e
			assert(found);
			if (beam.dz() < beam_depth) continue; // too short, skip
			assert(beam.is_strictly_normalized());
			// skip bottom and face against the roof (top may be partially visible when rotated)
			wood_mat.add_cube_to_verts(beam, WHITE, beam.get_llc(), (~get_face_mask(dim, dir) | EF_Z1));
		} // for n
		if (is_wall) continue; // below is for sloped roof tquads only
		// rotate to match slope of roof
		point rot_pt; // point where roof meets attic floor
		rot_pt[ dim] = bcube.d[dim][dir];
		rot_pt[!dim] = bcube.get_center_dim(dim); // doesn't matter?
		rot_pt.z     = bcube.z1(); // floor
		vector3d const rot_axis(dim ? -plus_x : plus_y);
		float const rot_angle((dir ? 1.0 : -1.0)*atan2(run_len, height));
		rotate_verts(wood_mat.quad_verts, rot_axis, rot_angle, rot_pt, qv_start);

		if (num_edges == 3) { // trapezoid case: add diag beam along both angled edges
			for (unsigned e = 0; e < num_edges; ++e) {
				edge_t const &E(edges[e]);
				if (E.p[0].z == E.p[1].z) continue; // not an angled edge
				bool const low_ix(E.p[1].z == bcube.z1());
				point const &lo(E.p[low_ix]), &hi(E.p[!low_ix]);
				vector3d const edge_delta(hi - lo);
				float const edge_len(edge_delta.mag());
				vector3d const edge_dir(edge_delta/edge_len);
				beam.set_from_point(lo);
				beam.z2() += edge_len; // will be correct after rotation
				beam.expand_in_dim(!dim, beam_hwidth);
				beam.d[dim][!dir] = beam.d[dim][dir] + dir_sign*beam_depth;
				unsigned const qv_start_angled(wood_mat.quad_verts.size());
				wood_mat.add_cube_to_verts(beam, WHITE, beam.get_llc(), (~get_face_mask(dim, dir) | EF_Z1));
				// rotate into place
				// TODO: not quite correct - need to rotate about Z now, or draw as extruded polygon
				vector3d const axis(cross_product(edge_dir, plus_z));
				float const angle(get_angle(plus_z, edge_dir));
				rotate_verts(wood_mat.quad_verts, axis, angle, lo, qv_start_angled);
			} // for e
		}
		if (tq.npts == 4 && dir == 0) { // add beam along the roofline for this quad
			beam = bcube;
			beam.z2() -= beam_hwidth*height/run_len; // shift to just touching the roof at the top
			beam.z1()  = beam.z2() - beam_depth;
			set_wall_width(beam, bcube.d[dim][!dir], beam_hwidth, dim); // inside/middle edge

			if (num_edges == 3) { // trapezoid case (optimization)
				swap(beam.d[!dim][0], beam.d[!dim][1]); // start denormalized

				for (unsigned n = 0; n < 4; ++n) { // find the span of the top of the roofline
					if (tq.pts[n].z != bcube.z2()) continue; // point not at peak of roof
					min_eq(beam.d[!dim][0], tq.pts[n][!dim]);
					max_eq(beam.d[!dim][1], tq.pts[n][!dim]);
				}
			}
			assert(beam.is_strictly_normalized());
			beam.expand_in_dim(!dim, -epsilon); // prevent Z-fighting
			if (beam.get_sz_dim(!dim) > beam_width) {wood_mat.add_cube_to_verts(beam, WHITE, beam.get_llc(), EF_Z2);} // skip top
		}
	} // for i
}
