/****************************************************************************
 *      photonintegr.cc: integrator for photon mapping and final gather
 *      This is part of the yafaray package
 *      Copyright (C) 2006  Mathias Wein (Lynx)
 *		Copyright (C) 2009  Rodrigo Placencia (DarkTide)
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
 */

#include <integrators/photonintegr.h>
#include <core_api/logging.h>
#include <core_api/session.h>
#include <core_api/volume.h>
#include <core_api/params.h>
#include <core_api/scene.h>

BEGIN_YAFRAY

void PhotonIntegrator::preGatherWorker(PreGatherData *gdata, float ds_rad, int n_search)
{
	unsigned int start, end, total;
	float ds_radius_2 = ds_rad * ds_rad;

	gdata->mutx_.lock();
	start = gdata->fetched_;
	total = gdata->rad_points_.size();
	end = gdata->fetched_ = std::min(total, start + 32);
	gdata->mutx_.unlock();

	FoundPhoton *gathered = new FoundPhoton[n_search];

	float radius = 0.f;
	float i_scale = 1.f / ((float)gdata->diffuse_map_->nPaths() * M_PI);
	float scale = 0.f;

	while(start < total)
	{
		for(unsigned int n = start; n < end; ++n)
		{
			radius = ds_radius_2;//actually the square radius...
			int n_gathered = gdata->diffuse_map_->gather(gdata->rad_points_[n].pos_, gathered, n_search, radius);

			Vec3 rnorm = gdata->rad_points_[n].normal_;

			Rgb sum(0.0);

			if(n_gathered > 0)
			{
				scale = i_scale / radius;

				for(int i = 0; i < n_gathered; ++i)
				{
					Vec3 pdir = gathered[i].photon_->direction();

					if(rnorm * pdir > 0.f) sum += gdata->rad_points_[n].refl_ * scale * gathered[i].photon_->color();
					else sum += gdata->rad_points_[n].transm_ * scale * gathered[i].photon_->color();
				}
			}

			gdata->radiance_vec_[n] = Photon(rnorm, gdata->rad_points_[n].pos_, sum);
		}
		gdata->mutx_.lock();
		start = gdata->fetched_;
		end = gdata->fetched_ = std::min(total, start + 32);
		gdata->pbar_->update(32);
		gdata->mutx_.unlock();
	}
	delete[] gathered;
}

PhotonIntegrator::PhotonIntegrator(unsigned int d_photons, unsigned int c_photons, bool transp_shad, int shadow_depth, float ds_rad, float c_rad)
{
	use_photon_caustics_ = true;
	use_photon_diffuse_ = true;
	type_ = Surface;
	tr_shad_ = transp_shad;
	final_gather_ = true;
	n_diffuse_photons_ = d_photons;
	n_caus_photons_ = c_photons;
	s_depth_ = shadow_depth;
	ds_radius_ = ds_rad;
	caus_radius_ = c_rad;
	r_depth_ = 6;
	max_bounces_ = 5;
	integrator_name_ = "PhotonMap";
	integrator_short_name_ = "PM";
}

PhotonIntegrator::~PhotonIntegrator()
{
	// Empty
}


void PhotonIntegrator::causticWorker(PhotonMap *caustic_map, int thread_id, const Scene *scene, unsigned int n_caus_photons, const Pdf1D *light_power_d, int num_c_lights, const std::string &integrator_name, const std::vector<Light *> &tmplights, int caus_depth, ProgressBar *pb, int pb_step, unsigned int &total_photons_shot, int max_bounces)
{
	Ray ray;
	float light_num_pdf, light_pdf, s_1, s_2, s_3, s_4, s_5, s_6, s_7, s_l;
	Rgb pcol;

	//shoot photons
	bool done = false;
	unsigned int curr = 0;

	SurfacePoint sp;
	RenderState state;
	unsigned char userdata[USER_DATA_SIZE + 7];
	state.userdata_ = (void *)(&userdata[7] - (((size_t)&userdata[7]) & 7));   // pad userdata to 8 bytes
	state.cam_ = scene->getCamera();

	float f_num_lights = (float)num_c_lights;
	unsigned int n_caus_photons_thread = 1 + ((n_caus_photons - 1) / scene->getNumThreadsPhotons());

	std::vector<Photon> local_caustic_photons;
	local_caustic_photons.clear();
	local_caustic_photons.reserve(n_caus_photons_thread);

	float inv_caust_photons = 1.f / (float)n_caus_photons;

	while(!done)
	{
		unsigned int haltoncurr = curr + n_caus_photons_thread * thread_id;

		state.chromatic_ = true;
		state.wavelength_ = scrHalton__(5, haltoncurr);

		s_1 = riVdC__(haltoncurr);
		s_2 = scrHalton__(2, haltoncurr);
		s_3 = scrHalton__(3, haltoncurr);
		s_4 = scrHalton__(4, haltoncurr);

		s_l = float(haltoncurr) * inv_caust_photons;
		int light_num = light_power_d->dSample(s_l, &light_num_pdf);

		if(light_num >= num_c_lights)
		{
			caustic_map->mutx_.lock();
			Y_ERROR << integrator_name << ": lightPDF sample error! " << s_l << "/" << light_num << YENDL;
			caustic_map->mutx_.unlock();
			return;
		}

		pcol = tmplights[light_num]->emitPhoton(s_1, s_2, s_3, s_4, ray, light_pdf);
		ray.tmin_ = scene->ray_min_dist_;
		ray.tmax_ = -1.0;
		pcol *= f_num_lights * light_pdf / light_num_pdf; //remember that lightPdf is the inverse of th pdf, hence *=...
		if(pcol.isBlack())
		{
			++curr;
			done = (curr >= n_caus_photons_thread);
			continue;
		}
		int n_bounces = 0;
		bool caustic_photon = false;
		bool direct_photon = true;
		const Material *material = nullptr;
		Bsdf_t bsdfs;

		while(scene->intersect(ray, sp))
		{
			if(std::isnan(pcol.r_) || std::isnan(pcol.g_) || std::isnan(pcol.b_))
			{
				caustic_map->mutx_.lock();
				Y_WARNING << integrator_name << ": NaN  on photon color for light" << light_num + 1 << "." << YENDL;
				caustic_map->mutx_.unlock();
				continue;
			}

			Rgb transm(1.f);
			Rgb vcol(0.f);
			const VolumeHandler *vol = nullptr;

			if(material)
			{
				if((bsdfs & BsdfVolumetric) && (vol = material->getVolumeHandler(sp.ng_ * -ray.dir_ < 0)))
				{
					if(vol->transmittance(state, ray, vcol)) transm = vcol;
				}
			}

			Vec3 wi = -ray.dir_, wo;
			material = sp.material_;
			material->initBsdf(state, sp, bsdfs);

			if(bsdfs & BsdfDiffuse)
			{
				if(caustic_photon)
				{
					Photon np(wi, sp.p_, pcol);
					local_caustic_photons.push_back(np);
				}
			}

			// need to break in the middle otherwise we scatter the photon and then discard it => redundant
			if(n_bounces == max_bounces) break;
			// scatter photon
			int d_5 = 3 * n_bounces + 5;

			s_5 = scrHalton__(d_5, haltoncurr);
			s_6 = scrHalton__(d_5 + 1, haltoncurr);
			s_7 = scrHalton__(d_5 + 2, haltoncurr);

			PSample sample(s_5, s_6, s_7, BsdfAll, pcol, transm);

			bool scattered = material->scatterPhoton(state, sp, wi, wo, sample);
			if(!scattered) break; //photon was absorped.

			pcol = sample.color_;

			caustic_photon = ((sample.sampled_flags_ & (BsdfGlossy | BsdfSpecular | BsdfDispersive)) && direct_photon) ||
							 ((sample.sampled_flags_ & (BsdfGlossy | BsdfSpecular | BsdfFilter | BsdfDispersive)) && caustic_photon);
			direct_photon = (sample.sampled_flags_ & BsdfFilter) && direct_photon;

			if(state.chromatic_ && (sample.sampled_flags_ & BsdfDispersive))
			{
				state.chromatic_ = false;
				Rgb wl_col;
				wl2Rgb__(state.wavelength_, wl_col);
				pcol *= wl_col;
			}

			ray.from_ = sp.p_;
			ray.dir_ = wo;
			ray.tmin_ = scene->ray_min_dist_;
			ray.tmax_ = -1.0;
			++n_bounces;
		}
		++curr;
		if(curr % pb_step == 0)
		{
			pb->mutx_.lock();
			pb->update();
			pb->mutx_.unlock();
			if(scene->getSignals() & Y_SIG_ABORT) { return; }
		}
		done = (curr >= n_caus_photons_thread);
	}
	caustic_map->mutx_.lock();
	caustic_map->appendVector(local_caustic_photons, curr);
	total_photons_shot += curr;
	caustic_map->mutx_.unlock();
}

void PhotonIntegrator::diffuseWorker(PhotonMap *diffuse_map, int thread_id, const Scene *scene, unsigned int n_diffuse_photons, const Pdf1D *light_power_d, int num_d_lights, const std::string &integrator_name, const std::vector<Light *> &tmplights, ProgressBar *pb, int pb_step, unsigned int &total_photons_shot, int max_bounces, bool final_gather, PreGatherData &pgdat)
{
	Ray ray;
	float light_num_pdf, light_pdf, s_1, s_2, s_3, s_4, s_5, s_6, s_7, s_l;
	Rgb pcol;

	//shoot photons
	bool done = false;
	unsigned int curr = 0;

	SurfacePoint sp;
	RenderState state;
	unsigned char userdata[USER_DATA_SIZE + 7];
	state.userdata_ = (void *)(&userdata[7] - (((size_t)&userdata[7]) & 7));   // pad userdata to 8 bytes
	state.cam_ = scene->getCamera();

	float f_num_lights = (float)num_d_lights;

	unsigned int n_diffuse_photons_thread = 1 + ((n_diffuse_photons - 1) / scene->getNumThreadsPhotons());

	std::vector<Photon> local_diffuse_photons;
	std::vector<RadData> local_rad_points;

	local_diffuse_photons.clear();
	local_diffuse_photons.reserve(n_diffuse_photons_thread);
	local_rad_points.clear();

	float inv_diff_photons = 1.f / (float)n_diffuse_photons;

	while(!done)
	{
		unsigned int haltoncurr = curr + n_diffuse_photons_thread * thread_id;

		s_1 = riVdC__(haltoncurr);
		s_2 = scrHalton__(2, haltoncurr);
		s_3 = scrHalton__(3, haltoncurr);
		s_4 = scrHalton__(4, haltoncurr);

		s_l = float(haltoncurr) * inv_diff_photons;
		int light_num = light_power_d->dSample(s_l, &light_num_pdf);
		if(light_num >= num_d_lights)
		{
			diffuse_map->mutx_.lock();
			Y_ERROR << integrator_name << ": lightPDF sample error! " << s_l << "/" << light_num << YENDL;
			diffuse_map->mutx_.unlock();
			return;
		}

		pcol = tmplights[light_num]->emitPhoton(s_1, s_2, s_3, s_4, ray, light_pdf);
		ray.tmin_ = scene->ray_min_dist_;
		ray.tmax_ = -1.0;
		pcol *= f_num_lights * light_pdf / light_num_pdf; //remember that lightPdf is the inverse of th pdf, hence *=...

		if(pcol.isBlack())
		{
			++curr;
			done = (curr >= n_diffuse_photons_thread);
			continue;
		}

		int n_bounces = 0;
		bool caustic_photon = false;
		bool direct_photon = true;
		const Material *material = nullptr;
		Bsdf_t bsdfs;

		while(scene->intersect(ray, sp))
		{
			if(std::isnan(pcol.r_) || std::isnan(pcol.g_) || std::isnan(pcol.b_))
			{
				diffuse_map->mutx_.lock();
				Y_WARNING << integrator_name << ": NaN  on photon color for light" << light_num + 1 << "." << YENDL;
				diffuse_map->mutx_.unlock();
				continue;
			}

			Rgb transm(1.f);
			Rgb vcol(0.f);
			const VolumeHandler *vol = nullptr;

			if(material)
			{
				if((bsdfs & BsdfVolumetric) && (vol = material->getVolumeHandler(sp.ng_ * -ray.dir_ < 0)))
				{
					if(vol->transmittance(state, ray, vcol)) transm = vcol;
				}
			}

			Vec3 wi = -ray.dir_, wo;
			material = sp.material_;
			material->initBsdf(state, sp, bsdfs);

			if(bsdfs & (BsdfDiffuse))
			{
				//deposit photon on surface
				if(!caustic_photon)
				{
					Photon np(wi, sp.p_, pcol);
					local_diffuse_photons.push_back(np);
				}
				// create entry for radiance photon:
				// don't forget to choose subset only, face normal forward; geometric vs. smooth normal?
				if(final_gather && ourRandom__() < 0.125 && !caustic_photon)
				{
					Vec3 n = FACE_FORWARD(sp.ng_, sp.n_, wi);
					RadData rd(sp.p_, n);
					rd.refl_ = material->getReflectivity(state, sp, BsdfDiffuse | BsdfGlossy | BsdfReflect);
					rd.transm_ = material->getReflectivity(state, sp, BsdfDiffuse | BsdfGlossy | BsdfTransmit);
					local_rad_points.push_back(rd);
				}
			}
			// need to break in the middle otherwise we scatter the photon and then discard it => redundant
			if(n_bounces == max_bounces) break;
			// scatter photon
			int d_5 = 3 * n_bounces + 5;

			s_5 = scrHalton__(d_5, haltoncurr);
			s_6 = scrHalton__(d_5 + 1, haltoncurr);
			s_7 = scrHalton__(d_5 + 2, haltoncurr);

			PSample sample(s_5, s_6, s_7, BsdfAll, pcol, transm);

			bool scattered = material->scatterPhoton(state, sp, wi, wo, sample);
			if(!scattered) break; //photon was absorped.

			pcol = sample.color_;

			caustic_photon = ((sample.sampled_flags_ & (BsdfGlossy | BsdfSpecular | BsdfDispersive)) && direct_photon) ||
							 ((sample.sampled_flags_ & (BsdfGlossy | BsdfSpecular | BsdfFilter | BsdfDispersive)) && caustic_photon);
			direct_photon = (sample.sampled_flags_ & BsdfFilter) && direct_photon;

			ray.from_ = sp.p_;
			ray.dir_ = wo;
			ray.tmin_ = scene->ray_min_dist_;
			ray.tmax_ = -1.0;
			++n_bounces;
		}
		++curr;
		if(curr % pb_step == 0)
		{
			pb->mutx_.lock();
			pb->update();
			pb->mutx_.unlock();
			if(scene->getSignals() & Y_SIG_ABORT) { return; }
		}
		done = (curr >= n_diffuse_photons_thread);
	}
	diffuse_map->mutx_.lock();
	diffuse_map->appendVector(local_diffuse_photons, curr);
	total_photons_shot += curr;
	diffuse_map->mutx_.unlock();

	pgdat.mutx_.lock();
	pgdat.rad_points_.insert(std::end(pgdat.rad_points_), std::begin(local_rad_points), std::end(local_rad_points));
	pgdat.mutx_.unlock();
}

void PhotonIntegrator::photonMapKdTreeWorker(PhotonMap *photon_map)
{
	photon_map->updateTree();
}

bool PhotonIntegrator::preprocess()
{
	ProgressBar *pb;
	if(intpb_) pb = intpb_;
	else pb = new ConsoleProgressBar(80);

	lookup_rad_ = 4 * ds_radius_ * ds_radius_;

	std::stringstream set;
	g_timer__.addEvent("prepass");
	g_timer__.start("prepass");

	Y_INFO << integrator_name_ << ": Starting preprocess..." << YENDL;

	set << "Photon Mapping  ";

	if(tr_shad_)
	{
		set << "ShadowDepth=" << s_depth_ << "  ";
	}
	set << "RayDepth=" << r_depth_ << "  ";

	background_ = scene_->getBackground();
	lights_ = scene_->lights_;
	std::vector<Light *> tmplights;

	if(use_photon_caustics_)
	{
		set << "\nCaustic photons=" << n_caus_photons_ << " search=" << n_caus_search_ << " radius=" << caus_radius_ << " depth=" << caus_depth_ << "  ";
	}

	if(use_photon_diffuse_)
	{
		set << "\nDiffuse photons=" << n_diffuse_photons_ << " search=" << n_diffuse_search_ << " radius=" << ds_radius_ << "  ";
	}

	if(final_gather_)
	{
		set << " FG paths=" << n_paths_ << " bounces=" << gather_bounces_ << "  ";
	}

	if(photon_map_processing_ == PhotonsLoad)
	{
		bool caustic_map_failed_load = false;
		bool diffuse_map_failed_load = false;
		bool fg_radiance_map_failed_load = false;

		if(use_photon_caustics_)
		{
			pb->setTag("Loading caustic photon map from file...");
			const std::string filename = session__.getPathImageOutput() + "_caustic.photonmap";
			Y_INFO << integrator_name_ << ": Loading caustic photon map from: " << filename << ". If it does not match the scene you could have crashes and/or incorrect renders, USE WITH CARE!" << YENDL;
			if(session__.caustic_map_->load(filename)) Y_VERBOSE << integrator_name_ << ": Caustic map loaded." << YENDL;
			else caustic_map_failed_load = true;
		}

		if(use_photon_diffuse_)
		{
			pb->setTag("Loading diffuse photon map from file...");
			const std::string filename = session__.getPathImageOutput() + "_diffuse.photonmap";
			Y_INFO << integrator_name_ << ": Loading diffuse photon map from: " << filename << ". If it does not match the scene you could have crashes and/or incorrect renders, USE WITH CARE!" << YENDL;
			if(session__.diffuse_map_->load(filename)) Y_VERBOSE << integrator_name_ << ": Diffuse map loaded." << YENDL;
			else diffuse_map_failed_load = true;
		}

		if(use_photon_diffuse_ && final_gather_)
		{
			pb->setTag("Loading FG radiance photon map from file...");
			const std::string filename = session__.getPathImageOutput() + "_fg_radiance.photonmap";
			Y_INFO << integrator_name_ << ": Loading FG radiance photon map from: " << filename << ". If it does not match the scene you could have crashes and/or incorrect renders, USE WITH CARE!" << YENDL;
			if(session__.radiance_map_->load(filename)) Y_VERBOSE << integrator_name_ << ": FG radiance map loaded." << YENDL;
			else fg_radiance_map_failed_load = true;
		}

		if(caustic_map_failed_load || diffuse_map_failed_load || fg_radiance_map_failed_load)
		{
			photon_map_processing_ = PhotonsGenerateAndSave;
			Y_WARNING << integrator_name_ << ": photon maps loading failed, changing to Generate and Save mode." << YENDL;
		}
	}

	if(photon_map_processing_ == PhotonsReuse)
	{
		if(use_photon_caustics_)
		{
			Y_INFO << integrator_name_ << ": Reusing caustics photon map from memory. If it does not match the scene you could have crashes and/or incorrect renders, USE WITH CARE!" << YENDL;
			if(session__.caustic_map_->nPhotons() == 0)
			{
				Y_WARNING << integrator_name_ << ": Caustic photon map enabled but empty, cannot be reused: changing to Generate mode." << YENDL;
				photon_map_processing_ = PhotonsGenerateOnly;
			}
		}

		if(use_photon_diffuse_)
		{
			Y_INFO << integrator_name_ << ": Reusing diffuse photon map from memory. If it does not match the scene you could have crashes and/or incorrect renders, USE WITH CARE!" << YENDL;
			if(session__.diffuse_map_->nPhotons() == 0)
			{
				Y_WARNING << integrator_name_ << ": Diffuse photon map enabled but empty, cannot be reused: changing to Generate mode." << YENDL;
				photon_map_processing_ = PhotonsGenerateOnly;
			}
		}

		if(final_gather_)
		{
			Y_INFO << integrator_name_ << ": Reusing FG radiance photon map from memory. If it does not match the scene you could have crashes and/or incorrect renders, USE WITH CARE!" << YENDL;
			if(session__.radiance_map_->nPhotons() == 0)
			{
				Y_WARNING << integrator_name_ << ": FG radiance photon map enabled but empty, cannot be reused: changing to Generate mode." << YENDL;
				photon_map_processing_ = PhotonsGenerateOnly;
			}
		}
	}

	if(photon_map_processing_ == PhotonsLoad)
	{
		set << " (loading photon maps from file)";
	}
	else if(photon_map_processing_ == PhotonsReuse)
	{
		set << " (reusing photon maps from memory)";
	}
	else if(photon_map_processing_ == PhotonsGenerateAndSave) set << " (saving photon maps to file)";

	if(photon_map_processing_ == PhotonsLoad || photon_map_processing_ == PhotonsReuse)
	{
		g_timer__.stop("prepass");
		Y_INFO << integrator_name_ << ": Photonmap building time: " << std::fixed << std::setprecision(1) << g_timer__.getTime("prepass") << "s" << YENDL;

		set << " [" << std::fixed << std::setprecision(1) << g_timer__.getTime("prepass") << "s" << "]";

		logger__.appendRenderSettings(set.str());

		for(std::string line; std::getline(set, line, '\n');) Y_VERBOSE << line << YENDL;

		return true;
	}

	session__.diffuse_map_->clear();
	session__.diffuse_map_->setNumPaths(0);
	session__.diffuse_map_->reserveMemory(n_diffuse_photons_);
	session__.diffuse_map_->setNumThreadsPkDtree(scene_->getNumThreadsPhotons());

	session__.caustic_map_->clear();
	session__.caustic_map_->setNumPaths(0);
	session__.caustic_map_->reserveMemory(n_caus_photons_);
	session__.caustic_map_->setNumThreadsPkDtree(scene_->getNumThreadsPhotons());

	session__.radiance_map_->clear();
	session__.radiance_map_->setNumPaths(0);
	session__.radiance_map_->setNumThreadsPkDtree(scene_->getNumThreadsPhotons());

	Ray ray;
	float light_num_pdf, light_pdf;
	int num_c_lights = 0;
	int num_d_lights = 0;
	float f_num_lights = 0.f;
	float *energies = nullptr;
	Rgb pcol;

	//shoot photons
	unsigned int curr = 0;
	// for radiance map:
	PreGatherData pgdat(session__.diffuse_map_);

	SurfacePoint sp;
	RenderState state;
	unsigned char userdata[USER_DATA_SIZE + 7];
	state.userdata_ = (void *)(&userdata[7] - (((size_t)&userdata[7]) & 7));   // pad userdata to 8 bytes
	state.cam_ = scene_->getCamera();
	int pb_step;

	tmplights.clear();

	for(int i = 0; i < (int)lights_.size(); ++i)
	{
		if(lights_[i]->shootsDiffuseP())
		{
			num_d_lights++;
			tmplights.push_back(lights_[i]);
		}
	}

	if(num_d_lights == 0)
	{
		Y_WARNING << integrator_name_ << ": No lights found that can shoot diffuse photons, disabling Diffuse photon processing" << YENDL;
		enableDiffuse(false);
	}

	if(use_photon_diffuse_)
	{
		f_num_lights = (float)num_d_lights;
		energies = new float[num_d_lights];

		for(int i = 0; i < num_d_lights; ++i) energies[i] = tmplights[i]->totalEnergy().energy();

		light_power_d_ = new Pdf1D(energies, num_d_lights);

		Y_VERBOSE << integrator_name_ << ": Light(s) photon color testing for diffuse map:" << YENDL;
		for(int i = 0; i < num_d_lights; ++i)
		{
			pcol = tmplights[i]->emitPhoton(.5, .5, .5, .5, ray, light_pdf);
			light_num_pdf = light_power_d_->func_[i] * light_power_d_->inv_integral_;
			pcol *= f_num_lights * light_pdf / light_num_pdf; //remember that lightPdf is the inverse of the pdf, hence *=...
			Y_VERBOSE << integrator_name_ << ": Light [" << i + 1 << "] Photon col:" << pcol << " | lnpdf: " << light_num_pdf << YENDL;
		}

		delete[] energies;

		//shoot photons
		curr = 0;

		Y_INFO << integrator_name_ << ": Building diffuse photon map..." << YENDL;

		pb->init(128);
		pb_step = std::max(1U, n_diffuse_photons_ / 128);
		pb->setTag("Building diffuse photon map...");
		//Pregather diffuse photons

		int n_threads = scene_->getNumThreadsPhotons();

		n_diffuse_photons_ = std::max((unsigned int) n_threads, (n_diffuse_photons_ / n_threads) * n_threads); //rounding the number of diffuse photons so it's a number divisible by the number of threads (distribute uniformly among the threads). At least 1 photon per thread

		Y_PARAMS << integrator_name_ << ": Shooting " << n_diffuse_photons_ << " photons across " << n_threads << " threads (" << (n_diffuse_photons_ / n_threads) << " photons/thread)" << YENDL;

		if(n_threads >= 2)
		{
			std::vector<std::thread> threads;
			for(int i = 0; i < n_threads; ++i) threads.push_back(std::thread(&PhotonIntegrator::diffuseWorker, this, session__.diffuse_map_, i, scene_, n_diffuse_photons_, light_power_d_, num_d_lights, std::ref(integrator_name_), tmplights, pb, pb_step, std::ref(curr), max_bounces_, final_gather_, std::ref(pgdat)));
			for(auto &t : threads) t.join();
		}
		else
		{
			bool done = false;

			float inv_diff_photons = 1.f / (float)n_diffuse_photons_;
			float s_1, s_2, s_3, s_4, s_5, s_6, s_7, s_l;

			while(!done)
			{
				if(scene_->getSignals() & Y_SIG_ABORT) {  pb->done(); if(!intpb_) delete pb; return false; }

				s_1 = riVdC__(curr);
				s_2 = scrHalton__(2, curr);
				s_3 = scrHalton__(3, curr);
				s_4 = scrHalton__(4, curr);

				s_l = float(curr) * inv_diff_photons;
				int light_num = light_power_d_->dSample(s_l, &light_num_pdf);
				if(light_num >= num_d_lights)
				{
					Y_ERROR << integrator_name_ << ": lightPDF sample error! " << s_l << "/" << light_num << "... stopping now." << YENDL;
					delete light_power_d_;
					return false;
				}

				pcol = tmplights[light_num]->emitPhoton(s_1, s_2, s_3, s_4, ray, light_pdf);
				ray.tmin_ = scene_->ray_min_dist_;
				ray.tmax_ = -1.0;
				pcol *= f_num_lights * light_pdf / light_num_pdf; //remember that lightPdf is the inverse of th pdf, hence *=...

				if(pcol.isBlack())
				{
					++curr;
					done = (curr >= n_diffuse_photons_);
					continue;
				}

				int n_bounces = 0;
				bool caustic_photon = false;
				bool direct_photon = true;
				const Material *material = nullptr;
				Bsdf_t bsdfs;

				while(scene_->intersect(ray, sp))
				{
					if(std::isnan(pcol.r_) || std::isnan(pcol.g_) || std::isnan(pcol.b_))
					{
						Y_WARNING << integrator_name_ << ": NaN  on photon color for light" << light_num + 1 << "." << YENDL;
						continue;
					}

					Rgb transm(1.f);
					Rgb vcol(0.f);
					const VolumeHandler *vol = nullptr;

					if(material)
					{
						if((bsdfs & BsdfVolumetric) && (vol = material->getVolumeHandler(sp.ng_ * -ray.dir_ < 0)))
						{
							if(vol->transmittance(state, ray, vcol)) transm = vcol;
						}
					}

					Vec3 wi = -ray.dir_, wo;
					material = sp.material_;
					material->initBsdf(state, sp, bsdfs);

					if(bsdfs & (BsdfDiffuse))
					{
						//deposit photon on surface
						if(!caustic_photon)
						{
							Photon np(wi, sp.p_, pcol);
							session__.diffuse_map_->pushPhoton(np);
							session__.diffuse_map_->setNumPaths(curr);
						}
						// create entry for radiance photon:
						// don't forget to choose subset only, face normal forward; geometric vs. smooth normal?
						if(final_gather_ && ourRandom__() < 0.125 && !caustic_photon)
						{
							Vec3 n = FACE_FORWARD(sp.ng_, sp.n_, wi);
							RadData rd(sp.p_, n);
							rd.refl_ = material->getReflectivity(state, sp, BsdfDiffuse | BsdfGlossy | BsdfReflect);
							rd.transm_ = material->getReflectivity(state, sp, BsdfDiffuse | BsdfGlossy | BsdfTransmit);
							pgdat.rad_points_.push_back(rd);
						}
					}
					// need to break in the middle otherwise we scatter the photon and then discard it => redundant
					if(n_bounces == max_bounces_) break;
					// scatter photon
					int d_5 = 3 * n_bounces + 5;

					s_5 = scrHalton__(d_5, curr);
					s_6 = scrHalton__(d_5 + 1, curr);
					s_7 = scrHalton__(d_5 + 2, curr);

					PSample sample(s_5, s_6, s_7, BsdfAll, pcol, transm);

					bool scattered = material->scatterPhoton(state, sp, wi, wo, sample);
					if(!scattered) break; //photon was absorped.

					pcol = sample.color_;

					caustic_photon = ((sample.sampled_flags_ & (BsdfGlossy | BsdfSpecular | BsdfDispersive)) && direct_photon) ||
									 ((sample.sampled_flags_ & (BsdfGlossy | BsdfSpecular | BsdfFilter | BsdfDispersive)) && caustic_photon);
					direct_photon = (sample.sampled_flags_ & BsdfFilter) && direct_photon;

					ray.from_ = sp.p_;
					ray.dir_ = wo;
					ray.tmin_ = scene_->ray_min_dist_;
					ray.tmax_ = -1.0;
					++n_bounces;
				}
				++curr;
				if(curr % pb_step == 0) pb->update();
				done = (curr >= n_diffuse_photons_);
			}
		}

		pb->done();
		pb->setTag("Diffuse photon map built.");
		Y_VERBOSE << integrator_name_ << ": Diffuse photon map built." << YENDL;
		Y_INFO << integrator_name_ << ": Shot " << curr << " photons from " << num_d_lights << " light(s)" << YENDL;

		delete light_power_d_;

		tmplights.clear();

		if(session__.diffuse_map_->nPhotons() < 50)
		{
			Y_ERROR << integrator_name_ << ": Too few diffuse photons, stopping now." << YENDL;
			return false;
		}

		Y_VERBOSE << integrator_name_ << ": Stored diffuse photons: " << session__.diffuse_map_->nPhotons() << YENDL;
	}
	else
	{
		Y_INFO << integrator_name_ << ": Diffuse photon mapping disabled, skipping..." << YENDL;
	}

	std::thread *diffuse_map_build_kd_tree_thread = nullptr;

	if(use_photon_diffuse_ && session__.diffuse_map_->nPhotons() > 0 && scene_->getNumThreadsPhotons() >= 2)
	{
		Y_INFO << integrator_name_ << ": Building diffuse photons kd-tree:" << YENDL;
		pb->setTag("Building diffuse photons kd-tree...");

		diffuse_map_build_kd_tree_thread = new std::thread(&PhotonIntegrator::photonMapKdTreeWorker, this, session__.diffuse_map_);
	}
	else

		if(use_photon_diffuse_ && session__.diffuse_map_->nPhotons() > 0)
		{
			Y_INFO << integrator_name_ << ": Building diffuse photons kd-tree:" << YENDL;
			pb->setTag("Building diffuse photons kd-tree...");
			session__.diffuse_map_->updateTree();
			Y_VERBOSE << integrator_name_ << ": Done." << YENDL;
		}

	for(int i = 0; i < (int)lights_.size(); ++i)
	{
		if(lights_[i]->shootsCausticP())
		{
			num_c_lights++;
			tmplights.push_back(lights_[i]);
		}
	}

	if(num_c_lights == 0)
	{
		Y_WARNING << integrator_name_ << ": No lights found that can shoot caustic photons, disabling Caustic photon processing" << YENDL;
		enableCaustics(false);
	}

	if(use_photon_caustics_)
	{
		curr = 0;

		f_num_lights = (float)num_c_lights;
		energies = new float[num_c_lights];

		for(int i = 0; i < num_c_lights; ++i) energies[i] = tmplights[i]->totalEnergy().energy();

		light_power_d_ = new Pdf1D(energies, num_c_lights);

		Y_VERBOSE << integrator_name_ << ": Light(s) photon color testing for caustics map:" << YENDL;
		for(int i = 0; i < num_c_lights; ++i)
		{
			pcol = tmplights[i]->emitPhoton(.5, .5, .5, .5, ray, light_pdf);
			light_num_pdf = light_power_d_->func_[i] * light_power_d_->inv_integral_;
			pcol *= f_num_lights * light_pdf / light_num_pdf; //remember that lightPdf is the inverse of the pdf, hence *=...
			Y_VERBOSE << integrator_name_ << ": Light [" << i + 1 << "] Photon col:" << pcol << " | lnpdf: " << light_num_pdf << YENDL;
		}

		delete[] energies;

		Y_INFO << integrator_name_ << ": Building caustics photon map..." << YENDL;
		pb->init(128);
		pb_step = std::max(1U, n_caus_photons_ / 128);
		pb->setTag("Building caustics photon map...");
		//Pregather caustic photons

		int n_threads = scene_->getNumThreadsPhotons();

		n_caus_photons_ = std::max((unsigned int) n_threads, (n_caus_photons_ / n_threads) * n_threads); //rounding the number of diffuse photons so it's a number divisible by the number of threads (distribute uniformly among the threads). At least 1 photon per thread

		Y_PARAMS << integrator_name_ << ": Shooting " << n_caus_photons_ << " photons across " << n_threads << " threads (" << (n_caus_photons_ / n_threads) << " photons/thread)" << YENDL;


		if(n_threads >= 2)
		{
			std::vector<std::thread> threads;
			for(int i = 0; i < n_threads; ++i) threads.push_back(std::thread(&PhotonIntegrator::causticWorker, this, session__.caustic_map_, i, scene_, n_caus_photons_, light_power_d_, num_c_lights, std::ref(integrator_name_), tmplights, caus_depth_, pb, pb_step, std::ref(curr), max_bounces_));
			for(auto &t : threads) t.join();
		}
		else
		{
			bool done = false;
			float inv_caust_photons = 1.f / (float)n_caus_photons_;
			float s_1, s_2, s_3, s_4, s_5, s_6, s_7, s_l;

			while(!done)
			{
				if(scene_->getSignals() & Y_SIG_ABORT) { pb->done(); if(!intpb_) delete pb; return false; }
				state.chromatic_ = true;
				state.wavelength_ = scrHalton__(5, curr);

				s_1 = riVdC__(curr);
				s_2 = scrHalton__(2, curr);
				s_3 = scrHalton__(3, curr);
				s_4 = scrHalton__(4, curr);

				s_l = float(curr) * inv_caust_photons;
				int light_num = light_power_d_->dSample(s_l, &light_num_pdf);

				if(light_num >= num_c_lights)
				{
					Y_ERROR << integrator_name_ << ": lightPDF sample error! " << s_l << "/" << light_num << "... stopping now." << YENDL;
					delete light_power_d_;
					return false;
				}

				pcol = tmplights[light_num]->emitPhoton(s_1, s_2, s_3, s_4, ray, light_pdf);
				ray.tmin_ = scene_->ray_min_dist_;
				ray.tmax_ = -1.0;
				pcol *= f_num_lights * light_pdf / light_num_pdf; //remember that lightPdf is the inverse of th pdf, hence *=...
				if(pcol.isBlack())
				{
					++curr;
					done = (curr >= n_caus_photons_);
					continue;
				}
				int n_bounces = 0;
				bool caustic_photon = false;
				bool direct_photon = true;
				const Material *material = nullptr;
				Bsdf_t bsdfs;

				while(scene_->intersect(ray, sp))
				{
					if(std::isnan(pcol.r_) || std::isnan(pcol.g_) || std::isnan(pcol.b_))
					{
						Y_WARNING << integrator_name_ << ": NaN  on photon color for light" << light_num + 1 << "." << YENDL;
						continue;
					}

					Rgb transm(1.f);
					Rgb vcol(0.f);
					const VolumeHandler *vol = nullptr;

					if(material)
					{
						if((bsdfs & BsdfVolumetric) && (vol = material->getVolumeHandler(sp.ng_ * -ray.dir_ < 0)))
						{
							if(vol->transmittance(state, ray, vcol)) transm = vcol;
						}
					}

					Vec3 wi = -ray.dir_, wo;
					material = sp.material_;
					material->initBsdf(state, sp, bsdfs);

					if(bsdfs & BsdfDiffuse)
					{
						if(caustic_photon)
						{
							Photon np(wi, sp.p_, pcol);
							session__.caustic_map_->pushPhoton(np);
							session__.caustic_map_->setNumPaths(curr);
						}
					}

					// need to break in the middle otherwise we scatter the photon and then discard it => redundant
					if(n_bounces == max_bounces_) break;
					// scatter photon
					int d_5 = 3 * n_bounces + 5;

					s_5 = scrHalton__(d_5, curr);
					s_6 = scrHalton__(d_5 + 1, curr);
					s_7 = scrHalton__(d_5 + 2, curr);

					PSample sample(s_5, s_6, s_7, BsdfAll, pcol, transm);

					bool scattered = material->scatterPhoton(state, sp, wi, wo, sample);
					if(!scattered) break; //photon was absorped.

					pcol = sample.color_;

					caustic_photon = ((sample.sampled_flags_ & (BsdfGlossy | BsdfSpecular | BsdfDispersive)) && direct_photon) ||
									 ((sample.sampled_flags_ & (BsdfGlossy | BsdfSpecular | BsdfFilter | BsdfDispersive)) && caustic_photon);
					direct_photon = (sample.sampled_flags_ & BsdfFilter) && direct_photon;

					if(state.chromatic_ && (sample.sampled_flags_ & BsdfDispersive))
					{
						state.chromatic_ = false;
						Rgb wl_col;
						wl2Rgb__(state.wavelength_, wl_col);
						pcol *= wl_col;
					}

					ray.from_ = sp.p_;
					ray.dir_ = wo;
					ray.tmin_ = scene_->ray_min_dist_;
					ray.tmax_ = -1.0;
					++n_bounces;
				}
				++curr;
				if(curr % pb_step == 0) pb->update();
				done = (curr >= n_caus_photons_);
			}
		}

		pb->done();
		pb->setTag("Caustics photon map built.");
		delete light_power_d_;

		Y_INFO << integrator_name_ << ": Shot " << curr << " caustic photons from " << num_c_lights << " light(s)." << YENDL;
		Y_VERBOSE << integrator_name_ << ": Stored caustic photons: " << session__.caustic_map_->nPhotons() << YENDL;
	}
	else
	{
		Y_INFO << integrator_name_ << ": Caustics photon mapping disabled, skipping..." << YENDL;
	}

	tmplights.clear();

	std::thread *caustic_map_build_kd_tree_thread = nullptr;

	if(use_photon_caustics_ && session__.caustic_map_->nPhotons() > 0 && scene_->getNumThreadsPhotons() >= 2)
	{
		Y_INFO << integrator_name_ << ": Building caustic photons kd-tree:" << YENDL;
		pb->setTag("Building caustic photons kd-tree...");

		caustic_map_build_kd_tree_thread = new std::thread(&PhotonIntegrator::photonMapKdTreeWorker, this, session__.caustic_map_);
	}
	else
	{
		if(use_photon_caustics_ && session__.caustic_map_->nPhotons() > 0)
		{
			Y_INFO << integrator_name_ << ": Building caustic photons kd-tree:" << YENDL;
			pb->setTag("Building caustic photons kd-tree...");
			session__.caustic_map_->updateTree();
			Y_VERBOSE << integrator_name_ << ": Done." << YENDL;
		}
	}

	if(use_photon_diffuse_ && session__.diffuse_map_->nPhotons() > 0 && scene_->getNumThreadsPhotons() >= 2 && diffuse_map_build_kd_tree_thread)
	{
		diffuse_map_build_kd_tree_thread->join();
		delete diffuse_map_build_kd_tree_thread;
		diffuse_map_build_kd_tree_thread = nullptr;

		Y_VERBOSE << integrator_name_ << ": Diffuse photon map: done." << YENDL;
	}

	if(!intpb_) delete pb;

	if(use_photon_diffuse_ && final_gather_) //create radiance map:
	{
		// == remove too close radiance points ==//
		kdtree::PointKdTree< RadData > *r_tree = new kdtree::PointKdTree< RadData >(pgdat.rad_points_, "FG Radiance Photon Map", scene_->getNumThreadsPhotons());
		std::vector< RadData > cleaned;
		for(unsigned int i = 0; i < pgdat.rad_points_.size(); ++i)
		{
			if(pgdat.rad_points_[i].use_)
			{
				cleaned.push_back(pgdat.rad_points_[i]);
				EliminatePhoton elim_proc(pgdat.rad_points_[i].normal_);
				float maxrad = 0.01f * ds_radius_; // 10% of diffuse search radius
				r_tree->lookup(pgdat.rad_points_[i].pos_, elim_proc, maxrad);
			}
		}
		pgdat.rad_points_.swap(cleaned);
		// ================ //
		int n_threads = scene_->getNumThreads();
		pgdat.radiance_vec_.resize(pgdat.rad_points_.size());
		if(intpb_) pgdat.pbar_ = intpb_;
		else pgdat.pbar_ = new ConsoleProgressBar(80);
		pgdat.pbar_->init(pgdat.rad_points_.size());
		pgdat.pbar_->setTag("Pregathering radiance data for final gathering...");

		std::vector<std::thread> threads;
		for(int i = 0; i < n_threads; ++i) threads.push_back(std::thread(&PhotonIntegrator::preGatherWorker, this, &pgdat, ds_radius_, n_diffuse_search_));
		for(auto &t : threads) t.join();

		session__.radiance_map_->swapVector(pgdat.radiance_vec_);
		pgdat.pbar_->done();
		pgdat.pbar_->setTag("Pregathering radiance data done...");
		if(!intpb_) delete pgdat.pbar_;
		Y_VERBOSE << integrator_name_ << ": Radiance tree built... Updating the tree..." << YENDL;
		session__.radiance_map_->updateTree();
		Y_VERBOSE << integrator_name_ << ": Done." << YENDL;

		delete r_tree;
		r_tree = nullptr;
	}

	if(use_photon_caustics_ && session__.caustic_map_->nPhotons() > 0 && scene_->getNumThreadsPhotons() >= 2 && caustic_map_build_kd_tree_thread)
	{
		caustic_map_build_kd_tree_thread->join();
		delete caustic_map_build_kd_tree_thread;
		caustic_map_build_kd_tree_thread = nullptr;

		Y_VERBOSE << integrator_name_ << ": Caustic photon map: done." << YENDL;
	}

	if(photon_map_processing_ == PhotonsGenerateAndSave)
	{
		if(use_photon_diffuse_)
		{
			pb->setTag("Saving diffuse photon map to file...");
			const std::string filename = session__.getPathImageOutput() + "_diffuse.photonmap";
			Y_INFO << integrator_name_ << ": Saving diffuse photon map to: " << filename << YENDL;
			if(session__.diffuse_map_->save(filename)) Y_VERBOSE << integrator_name_ << ": Diffuse map saved." << YENDL;
		}

		if(use_photon_caustics_)
		{
			pb->setTag("Saving caustic photon map to file...");
			const std::string filename = session__.getPathImageOutput() + "_caustic.photonmap";
			Y_INFO << integrator_name_ << ": Saving caustic photon map to: " << filename << YENDL;
			if(session__.caustic_map_->save(filename)) Y_VERBOSE << integrator_name_ << ": Caustic map saved." << YENDL;
		}

		if(use_photon_diffuse_ && final_gather_)
		{
			pb->setTag("Saving FG radiance photon map to file...");
			const std::string filename = session__.getPathImageOutput() + "_fg_radiance.photonmap";
			Y_INFO << integrator_name_ << ": Saving FG radiance photon map to: " << filename << YENDL;
			if(session__.radiance_map_->save(filename)) Y_VERBOSE << integrator_name_ << ": FG radiance map saved." << YENDL;
		}
	}

	g_timer__.stop("prepass");
	Y_INFO << integrator_name_ << ": Photonmap building time: " << std::fixed << std::setprecision(1) << g_timer__.getTime("prepass") << "s" << " (" << scene_->getNumThreadsPhotons() << " thread(s))" << YENDL;

	set << "| photon maps: " << std::fixed << std::setprecision(1) << g_timer__.getTime("prepass") << "s" << " [" << scene_->getNumThreadsPhotons() << " thread(s)]";

	logger__.appendRenderSettings(set.str());

	for(std::string line; std::getline(set, line, '\n');) Y_VERBOSE << line << YENDL;

	return true;
}

// final gathering: this is basically a full path tracer only that it uses the radiance map only
// at the path end. I.e. paths longer than 1 are only generated to overcome lack of local radiance detail.
// precondition: initBSDF of current spot has been called!
Rgb PhotonIntegrator::finalGathering(RenderState &state, const SurfacePoint &sp, const Vec3 &wo, ColorPasses &color_passes) const
{
	Rgb path_col(0.0);
	void *first_udat = state.userdata_;
	unsigned char userdata[USER_DATA_SIZE + 7];
	void *n_udat = (void *)(&userdata[7] - (((size_t)&userdata[7]) & 7));   // pad userdata to 8 bytes
	const VolumeHandler *vol;
	Rgb vcol(0.f);
	float w = 0.f;

	ColorPasses tmp_color_passes(scene_->getRenderPasses());

	int n_sampl = (int) ceilf(std::max(1, n_paths_ / state.ray_division_) * aa_indirect_sample_multiplier_);
	for(int i = 0; i < n_sampl; ++i)
	{
		Rgb throughput(1.0);
		float length = 0;
		SurfacePoint hit = sp;
		Vec3 pwo = wo;
		Ray pRay;
		Bsdf_t mat_bsd_fs;
		bool did_hit;
		const Material *p_mat = sp.material_;
		unsigned int offs = n_paths_ * state.pixel_sample_ + state.sampling_offs_ + i; // some redundancy here...
		Rgb lcol, scol;
		// "zero'th" FG bounce:
		float s_1 = riVdC__(offs);
		float s_2 = scrHalton__(2, offs);
		if(state.ray_division_ > 1)
		{
			s_1 = addMod1__(s_1, state.dc_1_);
			s_2 = addMod1__(s_2, state.dc_2_);
		}

		Sample s(s_1, s_2, BsdfDiffuse | BsdfReflect | BsdfTransmit); // glossy/dispersion/specular done via recursive raytracing
		scol = p_mat->sample(state, hit, pwo, pRay.dir_, s, w);

		scol *= w;
		if(scol.isBlack()) continue;

		pRay.tmin_ = scene_->ray_min_dist_;
		pRay.tmax_ = -1.0;
		pRay.from_ = hit.p_;
		throughput = scol;

		if(!(did_hit = scene_->intersect(pRay, hit))) continue;   //hit background

		p_mat = hit.material_;
		length = pRay.tmax_;
		state.userdata_ = n_udat;
		mat_bsd_fs = p_mat->getFlags();
		bool has_spec = mat_bsd_fs & BsdfSpecular;
		bool caustic = false;
		bool close = length < gather_dist_;
		bool do_bounce = close || has_spec;
		// further bounces construct a path just as with path tracing:
		for(int depth = 0; depth < gather_bounces_ && do_bounce; ++depth)
		{
			int d_4 = 4 * depth;
			pwo = -pRay.dir_;
			p_mat->initBsdf(state, hit, mat_bsd_fs);

			if((mat_bsd_fs & BsdfVolumetric) && (vol = p_mat->getVolumeHandler(hit.n_ * pwo < 0)))
			{
				if(vol->transmittance(state, pRay, vcol)) throughput *= vcol;
			}

			if(mat_bsd_fs & (BsdfDiffuse))
			{
				if(close)
				{
					lcol = estimateOneDirectLight(state, hit, pwo, offs, tmp_color_passes);
				}
				else if(caustic)
				{
					Vec3 sf = FACE_FORWARD(hit.ng_, hit.n_, pwo);
					const Photon *nearest = session__.radiance_map_->findNearest(hit.p_, sf, lookup_rad_);
					if(nearest) lcol = nearest->color();
				}

				if(close || caustic)
				{
					if(mat_bsd_fs & BsdfEmit) lcol += p_mat->emit(state, hit, pwo);
					path_col += lcol * throughput;
				}
			}

			s_1 = scrHalton__(d_4 + 3, offs);
			s_2 = scrHalton__(d_4 + 4, offs);

			if(state.ray_division_ > 1)
			{
				s_1 = addMod1__(s_1, state.dc_1_);
				s_2 = addMod1__(s_2, state.dc_2_);
			}

			Sample sb(s_1, s_2, (close) ? BsdfAll : BsdfAllSpecular | BsdfFilter);
			scol = p_mat->sample(state, hit, pwo, pRay.dir_, sb, w);

			if(sb.pdf_ <= 1.0e-6f)
			{
				did_hit = false;
				break;
			}

			scol *= w;

			pRay.tmin_ = scene_->ray_min_dist_;
			pRay.tmax_ = -1.0;
			pRay.from_ = hit.p_;
			throughput *= scol;
			did_hit = scene_->intersect(pRay, hit);

			if(!did_hit) //hit background
			{
				if(caustic && background_ && background_->hasIbl() && background_->shootsCaustic())
				{
					path_col += throughput * (*background_)(pRay, state, true);
				}
				break;
			}

			p_mat = hit.material_;
			length += pRay.tmax_;
			caustic = (caustic || !depth) && (sb.sampled_flags_ & (BsdfSpecular | BsdfFilter));
			close = length < gather_dist_;
			do_bounce = caustic || close;
		}

		if(did_hit)
		{
			p_mat->initBsdf(state, hit, mat_bsd_fs);
			if(mat_bsd_fs & (BsdfDiffuse | BsdfGlossy))
			{
				Vec3 sf = FACE_FORWARD(hit.ng_, hit.n_, -pRay.dir_);
				const Photon *nearest = session__.radiance_map_->findNearest(hit.p_, sf, lookup_rad_);
				if(nearest) lcol = nearest->color();
				if(mat_bsd_fs & BsdfEmit) lcol += p_mat->emit(state, hit, -pRay.dir_);
				path_col += lcol * throughput;
			}
		}
		state.userdata_ = first_udat;
	}
	return path_col / (float)n_sampl;
}

Rgba PhotonIntegrator::integrate(RenderState &state, DiffRay &ray, ColorPasses &color_passes, int additional_depth /*=0*/) const
{
	static int n_max = 0;
	static int calls = 0;
	++calls;
	Rgb col(0.0);
	float alpha;
	SurfacePoint sp;

	void *o_udat = state.userdata_;
	bool old_include_lights = state.include_lights_;

	if(transp_background_) alpha = 0.0;
	else alpha = 1.0;

	if(scene_->intersect(ray, sp))
	{
		unsigned char userdata[USER_DATA_SIZE + 7];
		state.userdata_ = (void *)(&userdata[7] - (((size_t)&userdata[7]) & 7));   // pad userdata to 8 bytes

		if(state.raylevel_ == 0)
		{
			state.chromatic_ = true;
			state.include_lights_ = true;
		}
		Bsdf_t bsdfs;
		int additionalDepth = 0;

		Vec3 wo = -ray.dir_;
		const Material *material = sp.material_;
		material->initBsdf(state, sp, bsdfs);

		if(additionalDepth < material->getAdditionalDepth()) additionalDepth = material->getAdditionalDepth();

		col += color_passes.probeAdd(PassIntEmit, material->emit(state, sp, wo), state.raylevel_ == 0);

		state.include_lights_ = false;

		if(use_photon_diffuse_ && final_gather_)
		{
			if(show_map_)
			{
				Vec3 n = FACE_FORWARD(sp.ng_, sp.n_, wo);
				const Photon *nearest = session__.radiance_map_->findNearest(sp.p_, n, lookup_rad_);
				if(nearest) col += nearest->color();
			}
			else
			{
				if(state.raylevel_ == 0 && color_passes.enabled(PassIntRadiance))
				{
					Vec3 n = FACE_FORWARD(sp.ng_, sp.n_, wo);
					const Photon *nearest = session__.radiance_map_->findNearest(sp.p_, n, lookup_rad_);
					if(nearest) color_passes(PassIntRadiance) = nearest->color();
				}

				// contribution of light emitting surfaces
				if(bsdfs & BsdfEmit) col += color_passes.probeAdd(PassIntEmit, material->emit(state, sp, wo), state.raylevel_ == 0);

				if(bsdfs & BsdfDiffuse)
				{
					col += estimateAllDirectLight(state, sp, wo, color_passes);;

					if(aa_clamp_indirect_ > 0.f)
					{
						Rgb tmp_col = finalGathering(state, sp, wo, color_passes);
						tmp_col.clampProportionalRgb(aa_clamp_indirect_);
						col += color_passes.probeSet(PassIntDiffuseIndirect, tmp_col, state.raylevel_ == 0);
					}
					else col += color_passes.probeSet(PassIntDiffuseIndirect, finalGathering(state, sp, wo, color_passes), state.raylevel_ == 0);
				}
			}
		}
		else
		{
			if(use_photon_diffuse_ && show_map_)
			{
				Vec3 n = FACE_FORWARD(sp.ng_, sp.n_, wo);
				const Photon *nearest = session__.diffuse_map_->findNearest(sp.p_, n, ds_radius_);
				if(nearest) col += nearest->color();
			}
			else
			{
				if(use_photon_diffuse_ && state.raylevel_ == 0 && color_passes.enabled(PassIntRadiance))
				{
					Vec3 n = FACE_FORWARD(sp.ng_, sp.n_, wo);
					const Photon *nearest = session__.radiance_map_->findNearest(sp.p_, n, lookup_rad_);
					if(nearest) color_passes(PassIntRadiance) = nearest->color();
				}

				if(bsdfs & BsdfEmit) col += color_passes.probeAdd(PassIntEmit, material->emit(state, sp, wo), state.raylevel_ == 0);

				if(bsdfs & BsdfDiffuse)
				{
					col += estimateAllDirectLight(state, sp, wo, color_passes);
				}

				FoundPhoton *gathered = (FoundPhoton *)alloca(n_diffuse_search_ * sizeof(FoundPhoton));
				float radius = ds_radius_; //actually the square radius...

				int n_gathered = 0;

				if(use_photon_diffuse_ && session__.diffuse_map_->nPhotons() > 0) n_gathered = session__.diffuse_map_->gather(sp.p_, gathered, n_diffuse_search_, radius);
				Rgb sum(0.0);
				if(use_photon_diffuse_ && n_gathered > 0)
				{
					if(n_gathered > n_max) n_max = n_gathered;

					float scale = 1.f / ((float)session__.diffuse_map_->nPaths() * radius * M_PI);
					for(int i = 0; i < n_gathered; ++i)
					{
						Vec3 pdir = gathered[i].photon_->direction();
						Rgb surf_col = material->eval(state, sp, wo, pdir, BsdfDiffuse);

						col += color_passes.probeAdd(PassIntDiffuseIndirect, surf_col * scale * gathered[i].photon_->color(), state.raylevel_ == 0);
					}
				}
			}
		}

		// add caustics
		if(use_photon_caustics_ && bsdfs & BsdfDiffuse)
		{
			if(aa_clamp_indirect_ > 0.f)
			{
				Rgb tmp_col = estimateCausticPhotons(state, sp, wo);
				tmp_col.clampProportionalRgb(aa_clamp_indirect_);
				col += color_passes.probeSet(PassIntIndirect, tmp_col, state.raylevel_ == 0);
			}
			else col += color_passes.probeSet(PassIntIndirect, estimateCausticPhotons(state, sp, wo), state.raylevel_ == 0);
		}

		recursiveRaytrace(state, ray, bsdfs, sp, wo, col, alpha, color_passes, additionalDepth);

		if(color_passes.size() > 1 && state.raylevel_ == 0)
		{
			generateCommonRenderPasses(color_passes, state, sp, ray);

			if(color_passes.enabled(PassIntAo))
			{
				color_passes(PassIntAo) = sampleAmbientOcclusionPass(state, sp, wo);
			}

			if(color_passes.enabled(PassIntAoClay))
			{
				color_passes(PassIntAoClay) = sampleAmbientOcclusionPassClay(state, sp, wo);
			}
		}

		if(transp_refracted_background_)
		{
			float m_alpha = material->getAlpha(state, sp, wo);
			alpha = m_alpha + (1.f - m_alpha) * alpha;
		}
		else alpha = 1.0;
	}
	else //nothing hit, return background
	{
		if(background_ && !transp_refracted_background_)
		{
			col += color_passes.probeSet(PassIntEnv, (*background_)(ray, state), state.raylevel_ == 0);
		}
	}

	state.userdata_ = o_udat;
	state.include_lights_ = old_include_lights;

	Rgb col_vol_transmittance = scene_->vol_integrator_->transmittance(state, ray);
	Rgb col_vol_integration = scene_->vol_integrator_->integrate(state, ray, color_passes);

	if(transp_background_) alpha = std::max(alpha, 1.f - col_vol_transmittance.r_);

	color_passes.probeSet(PassIntVolumeTransmittance, col_vol_transmittance);
	color_passes.probeSet(PassIntVolumeIntegration, col_vol_integration);

	col = (col * col_vol_transmittance) + col_vol_integration;

	return Rgba(col, alpha);
}

Integrator *PhotonIntegrator::factory(ParamMap &params, RenderEnvironment &render)
{
	bool transp_shad = false;
	bool final_gather = true;
	bool show_map = false;
	int shadow_depth = 5;
	int raydepth = 5;
	int num_photons = 100000;
	int num_c_photons = 500000;
	int search = 50;
	int caustic_mix = 50;
	int bounces = 5;
	int fg_paths = 32;
	int fg_bounces = 2;
	float ds_rad = 0.1;
	float c_rad = 0.01;
	float gather_dist = 0.2;
	bool do_ao = false;
	int ao_samples = 32;
	double ao_dist = 1.0;
	Rgb ao_col(1.f);
	bool bg_transp = false;
	bool bg_transp_refract = false;
	bool caustics = true;
	bool diffuse = true;
	std::string photon_maps_processing_str = "generate";

	params.getParam("caustics", caustics);
	params.getParam("diffuse", diffuse);

	params.getParam("transpShad", transp_shad);
	params.getParam("shadowDepth", shadow_depth);
	params.getParam("raydepth", raydepth);
	params.getParam("photons", num_photons);
	params.getParam("cPhotons", num_c_photons);
	params.getParam("diffuseRadius", ds_rad);
	params.getParam("causticRadius", c_rad);
	params.getParam("search", search);
	caustic_mix = search;
	params.getParam("caustic_mix", caustic_mix);
	params.getParam("bounces", bounces);
	params.getParam("finalGather", final_gather);
	params.getParam("fg_samples", fg_paths);
	params.getParam("fg_bounces", fg_bounces);
	gather_dist = ds_rad;
	params.getParam("fg_min_pathlen", gather_dist);
	params.getParam("show_map", show_map);
	params.getParam("bg_transp", bg_transp);
	params.getParam("bg_transp_refract", bg_transp_refract);
	params.getParam("do_AO", do_ao);
	params.getParam("AO_samples", ao_samples);
	params.getParam("AO_distance", ao_dist);
	params.getParam("AO_color", ao_col);
	params.getParam("photon_maps_processing", photon_maps_processing_str);

	PhotonIntegrator *ite = new PhotonIntegrator(num_photons, num_c_photons, transp_shad, shadow_depth, ds_rad, c_rad);

	ite->use_photon_caustics_ = caustics;
	ite->use_photon_diffuse_ = diffuse;

	ite->r_depth_ = raydepth;
	ite->n_diffuse_search_ = search;
	ite->n_caus_search_ = caustic_mix;
	ite->final_gather_ = final_gather;
	ite->max_bounces_ = bounces;
	ite->caus_depth_ = bounces;
	ite->n_paths_ = fg_paths;
	ite->gather_bounces_ = fg_bounces;
	ite->show_map_ = show_map;
	ite->gather_dist_ = gather_dist;
	// Background settings
	ite->transp_background_ = bg_transp;
	ite->transp_refracted_background_ = bg_transp_refract;
	// AO settings
	ite->use_ambient_occlusion_ = do_ao;
	ite->ao_samples_ = ao_samples;
	ite->ao_dist_ = ao_dist;
	ite->ao_col_ = ao_col;

	if(photon_maps_processing_str == "generate-save") ite->photon_map_processing_ = PhotonsGenerateAndSave;
	else if(photon_maps_processing_str == "load") ite->photon_map_processing_ = PhotonsLoad;
	else if(photon_maps_processing_str == "reuse-previous") ite->photon_map_processing_ = PhotonsReuse;
	else ite->photon_map_processing_ = PhotonsGenerateOnly;

	return ite;
}

extern "C"
{

	YAFRAYPLUGIN_EXPORT void registerPlugin__(RenderEnvironment &render)
	{
		render.registerFactory("photonmapping", PhotonIntegrator::factory);
	}

}

END_YAFRAY
