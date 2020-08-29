/****************************************************************************
 *
 * 			camera.cc: Camera implementation
 *      This is part of the yafray package
 *      Copyright (C) 2002  Alejandro Conty Estévez
 *
 *      This library is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU Lesser General Public
 *      License as published by the Free Software Foundation; either
 *      version 2.1 of the License, or (at your option) any later version.
 *
 *      This library is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *      Lesser General Public License for more details.
 *
 *      You should have received a copy of the GNU Lesser General Public
 *      License along with this library; if not, write to the Free Software
 *      Foundation,Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#include <cameras/perspectiveCamera.h>
#include <core_api/environment.h>
#include <core_api/params.h>

BEGIN_YAFRAY

PerspectiveCamera::PerspectiveCamera(const Point3 &pos, const Point3 &look, const Point3 &up,
									 int resx, int resy, float aspect,
									 float df, float ap, float dofd, BokehType bt, BkhBiasType bbt, float bro,
									 float const near_clip_distance, float const far_clip_distance) :
		Camera(pos, look, up, resx, resy, aspect, near_clip_distance, far_clip_distance), bkhtype_(bt), bkhbias_(bbt), aperture_(ap), focal_distance_(df), dof_distance_(dofd)
{
	// Initialize camera specific plane coordinates
	setAxis(cam_x_, cam_y_, cam_z_);

	fdist_ = (look - pos).length();
	a_pix_ = aspect_ratio_ / (df * df);

	int ns = (int)bkhtype_;
	if((ns >= 3) && (ns <= 6))
	{
		float w = DEG_TO_RAD(bro), wi = (M_2PI) / (float)ns;
		ns = (ns + 2) * 2;
		ls_.resize(ns);
		for(int i = 0; i < ns; i += 2)
		{
			ls_[i] = fCos__(w);
			ls_[i + 1] = fSin__(w);
			w += wi;
		}
	}
}

PerspectiveCamera::~PerspectiveCamera()
{
}

void PerspectiveCamera::setAxis(const Vec3 &vx, const Vec3 &vy, const Vec3 &vz)
{
	cam_x_ = vx;
	cam_y_ = vy;
	cam_z_ = vz;

	dof_rt_ = aperture_ * cam_x_; // for dof, premul with aperture
	dof_up_ = aperture_ * cam_y_;

	vright_ = cam_x_;
	vup_ = aspect_ratio_ * cam_y_;
	vto_ = (cam_z_ * focal_distance_) - 0.5 * (vup_ + vright_);
	vup_ /= (float)resy_;
	vright_ /= (float)resx_;
}

void PerspectiveCamera::biasDist(float &r) const
{
	switch(bkhbias_)
	{
		case BbCenter:
			r = fSqrt__(fSqrt__(r) * r);
			break;
		case BbEdge:
			r = fSqrt__((float) 1.0 - r * r);
			break;
		default:
		case BbNone:
			r = fSqrt__(r);
	}
}

void PerspectiveCamera::sampleTsd(float r_1, float r_2, float &u, float &v) const
{
	float fn = (float)bkhtype_;
	int idx = int(r_1 * fn);
	r_1 = (r_1 - ((float)idx) / fn) * fn;
	biasDist(r_1);
	float b_1 = r_1 * r_2;
	float b_0 = r_1 - b_1;
	idx <<= 1;
	u = ls_[idx] * b_0 + ls_[idx + 2] * b_1;
	v = ls_[idx + 1] * b_0 + ls_[idx + 3] * b_1;
}

void PerspectiveCamera::getLensUv(float r_1, float r_2, float &u, float &v) const
{
	switch(bkhtype_)
	{
		case BkTri:
		case BkSqr:
		case BkPenta:
		case BkHexa:
			sampleTsd(r_1, r_2, u, v);
			break;
		case BkDisk2:
		case BkRing:
		{
			float w = (float)M_2PI * r_2;
			if(bkhtype_ == BkRing) r_1 = fSqrt__((float) 0.707106781 + (float) 0.292893218);
			else biasDist(r_1);
			u = r_1 * fCos__(w);
			v = r_1 * fSin__(w);
			break;
		}
		default:
		case BkDisk1:
			shirleyDisk__(r_1, r_2, u, v);
	}
}



Ray PerspectiveCamera::shootRay(float px, float py, float lu, float lv, float &wt) const
{
	Ray ray;
	wt = 1;	// for now always 1, except 0 for probe when outside sphere

	ray.from_ = position_;
	ray.dir_ = vright_ * px + vup_ * py + vto_;
	ray.dir_.normalize();

	ray.tmin_ = rayPlaneIntersection__(ray, near_plane_);
	ray.tmax_ = rayPlaneIntersection__(ray, far_plane_);

	if(aperture_ != 0)
	{
		float u, v;

		getLensUv(lu, lv, u, v);
		Vec3 li = dof_rt_ * u + dof_up_ * v;
		ray.from_ += Point3(li);
		ray.dir_ = (ray.dir_ * dof_distance_) - li;
		ray.dir_.normalize();
	}
	return ray;
}

Point3 PerspectiveCamera::screenproject(const Point3 &p) const
{
	Point3 s;
	Vec3 dir = p - position_;

	// project p to pixel plane:
	float dx = dir * cam_x_;
	float dy = dir * cam_y_;
	float dz = dir * cam_z_;

	s.x_ = 2.0f * dx * focal_distance_ / dz;
	s.y_ = -2.0f * dy * focal_distance_ / (dz * aspect_ratio_);
	s.z_ = 0;

	return s;
}

bool PerspectiveCamera::project(const Ray &wo, float lu, float lv, float &u, float &v, float &pdf) const
{
	// project wo to pixel plane:
	float dx = cam_x_ * wo.dir_;
	float dy = cam_y_ * wo.dir_;
	float dz = cam_z_ * wo.dir_;
	if(dz <= 0) return false;

	u = dx * focal_distance_ / dz;
	if(u < -0.5 || u > 0.5) return false;
	u = (u + 0.5) * (float) resx_;

	v = dy * focal_distance_ / (dz * aspect_ratio_);
	if(v < -0.5 || v > 0.5) return false;
	v = (v + 0.5) * (float) resy_;

	// pdf = 1/A_pix * r^2 / cos(forward, dir), where r^2 is also 1/cos(vto, dir)^2
	float cos_wo = dz; //camZ * wo.dir;
	pdf = 8.f * M_PI / (a_pix_ * cos_wo * cos_wo * cos_wo);
	return true;
}

bool PerspectiveCamera::sampleLense() const { return aperture_ != 0; }

Camera *PerspectiveCamera::factory(ParamMap &params, RenderEnvironment &render)
{
	std::string bkhtype = "disk1", bkhbias = "uniform";
	Point3 from(0, 1, 0), to(0, 0, 0), up(0, 1, 1);
	int resx = 320, resy = 200;
	float aspect = 1, dfocal = 1, apt = 0, dofd = 0, bkhrot = 0;
	float near_clip = 0.0f, far_clip = -1.0f;
	std::string view_name = "";

	params.getParam("from", from);
	params.getParam("to", to);
	params.getParam("up", up);
	params.getParam("resx", resx);
	params.getParam("resy", resy);
	params.getParam("focal", dfocal);
	params.getParam("aperture", apt);
	params.getParam("dof_distance", dofd);
	params.getParam("bokeh_type", bkhtype);
	params.getParam("bokeh_bias", bkhbias);
	params.getParam("bokeh_rotation", bkhrot);
	params.getParam("aspect_ratio", aspect);
	params.getParam("nearClip", near_clip);
	params.getParam("farClip", far_clip);
	params.getParam("view_name", view_name);

	BokehType bt = BkDisk1;
	if(bkhtype == "disk2")			bt = BkDisk2;
	else if(bkhtype == "triangle")	bt = BkTri;
	else if(bkhtype == "square")	bt = BkSqr;
	else if(bkhtype == "pentagon")	bt = BkPenta;
	else if(bkhtype == "hexagon")	bt = BkHexa;
	else if(bkhtype == "ring")		bt = BkRing;
	// bokeh bias
	BkhBiasType bbt = BbNone;
	if(bkhbias == "center") 		bbt = BbCenter;
	else if(bkhbias == "edge") 		bbt = BbEdge;

	PerspectiveCamera *cam = new PerspectiveCamera(from, to, up, resx, resy, aspect, dfocal, apt, dofd, bt, bbt, bkhrot, near_clip, far_clip);

	cam->view_name_ = view_name;

	return cam;
}

END_YAFRAY
