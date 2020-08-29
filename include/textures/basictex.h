#pragma once

#ifndef YAFARAY_BASICTEX_H
#define YAFARAY_BASICTEX_H

#include <core_api/texture.h>
#include <core_api/environment.h>
#include <textures/noise.h>
#include <utilities/buffer.h>

BEGIN_YAFRAY

class CloudsTexture final : public Texture
{
	public:
		static Texture *factory(ParamMap &params, RenderEnvironment &render);

	private:
		CloudsTexture(int dep, float sz, bool hd,
					  const Rgb &c_1, const Rgb &c_2,
					  const std::string &ntype, const std::string &btype);
		virtual ~CloudsTexture() override;
		virtual Rgba getColor(const Point3 &p, const MipMapParams *mipmap_params = nullptr) const override;
		virtual float getFloat(const Point3 &p, const MipMapParams *mipmap_params = nullptr) const override;
		virtual void getInterpolationStep(float &step) const override { step = size_; };

		int depth_, bias_;
		float size_;
		bool hard_;
		Rgb color_1_, color_2_;
		NoiseGenerator *n_gen_ = nullptr;
};


class MarbleTexture final : public Texture
{
	public:
		static Texture *factory(ParamMap &params, RenderEnvironment &render);

	private:
		MarbleTexture(int oct, float sz, const Rgb &c_1, const Rgb &c_2,
					  float turb, float shp, bool hrd, const std::string &ntype, const std::string &shape);
		virtual ~MarbleTexture() override
		{
			if(n_gen_)
			{
				delete n_gen_;
				n_gen_ = nullptr;
			}
		}

		virtual Rgba getColor(const Point3 &p, const MipMapParams *mipmap_params = nullptr) const override;
		virtual float getFloat(const Point3 &p, const MipMapParams *mipmap_params = nullptr) const override;
		virtual void getInterpolationStep(float &step) const override { step = size_; };

		int octaves_;
		Rgb color_1_, color_2_;
		float turb_, sharpness_, size_;
		bool hard_;
		NoiseGenerator *n_gen_ = nullptr;
		enum {Sin, Saw, Tri} wshape_;
};

class WoodTexture final : public Texture
{
	public:
		static Texture *factory(ParamMap &params, RenderEnvironment &render);

	private:
		WoodTexture(int oct, float sz, const Rgb &c_1, const Rgb &c_2, float turb,
					bool hrd, const std::string &ntype, const std::string &wtype, const std::string &shape);
		virtual ~WoodTexture() override
		{
			if(n_gen_)
			{
				delete n_gen_;
				n_gen_ = nullptr;
			}
		}

		virtual Rgba getColor(const Point3 &p, const MipMapParams *mipmap_params = nullptr) const override;
		virtual float getFloat(const Point3 &p, const MipMapParams *mipmap_params = nullptr) const override;
		virtual void getInterpolationStep(float &step) const override { step = size_; };

		int octaves_;
		Rgb color_1_, color_2_;
		float turb_, size_;
		bool hard_, rings_;
		NoiseGenerator *n_gen_ = nullptr;
		enum {Sin, Saw, Tri} wshape_;
};

class VoronoiTexture final : public Texture
{
	public:
		static Texture *factory(ParamMap &params, RenderEnvironment &render);

	private:
		VoronoiTexture(const Rgb &c_1, const Rgb &c_2,
					   int ct,
					   float w_1, float w_2, float w_3, float w_4,
					   float mex, float sz,
					   float isc, const std::string &dname);
		virtual Rgba getColor(const Point3 &p, const MipMapParams *mipmap_params = nullptr) const override;
		virtual float getFloat(const Point3 &p, const MipMapParams *mipmap_params = nullptr) const override;
		virtual void getInterpolationStep(float &step) const override { step = size_; };

		Rgb color_1_, color_2_;
		float w_1_, w_2_, w_3_, w_4_;	// feature weights
		float aw_1_, aw_2_, aw_3_, aw_4_;	// absolute value of above
		float size_;
		int coltype_;	// color return type
		float iscale_;	// intensity scale
		VoronoiNoiseGenerator v_gen_;
};

class MusgraveTexture final : public Texture
{
	public:
		static Texture *factory(ParamMap &params, RenderEnvironment &render);

	private:
		MusgraveTexture(const Rgb &c_1, const Rgb &c_2,
						float h, float lacu, float octs, float offs, float gain,
						float size, float iscale,
						const std::string &ntype, const std::string &mtype);
		virtual ~MusgraveTexture() override;
		virtual Rgba getColor(const Point3 &p, const MipMapParams *mipmap_params = nullptr) const override;
		virtual float getFloat(const Point3 &p, const MipMapParams *mipmap_params = nullptr) const override;
		virtual void getInterpolationStep(float &step) const override { step = size_; };

		Rgb color_1_, color_2_;
		float size_, iscale_;
		NoiseGenerator *n_gen_ = nullptr;
		Musgrave *m_gen_ = nullptr;
};

class DistortedNoiseTexture final : public Texture
{
	public:
		static Texture *factory(ParamMap &params, RenderEnvironment &render);

	private:
		DistortedNoiseTexture(const Rgb &c_1, const Rgb &c_2,
							  float distort, float size,
							  const std::string &noiseb_1, const std::string noiseb_2);
		virtual ~DistortedNoiseTexture() override;
		virtual Rgba getColor(const Point3 &p, const MipMapParams *mipmap_params = nullptr) const override;
		virtual float getFloat(const Point3 &p, const MipMapParams *mipmap_params = nullptr) const override;
		virtual void getInterpolationStep(float &step) const override { step = size_; };

		Rgb color_1_, color_2_;
		float distort_, size_;
		NoiseGenerator *n_gen_1_ = nullptr, *n_gen_2_ = nullptr;
};

/*! RGB cube.
	basically a simple visualization of the RGB color space,
	just goes r in x, g in y and b in z inside the unit cube. */

class RgbCubeTexture final : public Texture
{
	public:
		static Texture *factory(ParamMap &params, RenderEnvironment &render);

	private:
		RgbCubeTexture() = default;
		virtual Rgba getColor(const Point3 &p, const MipMapParams *mipmap_params = nullptr) const override;
		virtual float getFloat(const Point3 &p, const MipMapParams *mipmap_params = nullptr) const override;
};

class BlendTexture final : public Texture
{
	public:
		static Texture *factory(ParamMap &params, RenderEnvironment &render);

	private:
		enum ProgressionType : int { Linear, Quadratic, Easing, Diagonal, Spherical, QuadraticSphere, Radial };
		BlendTexture(const std::string &stype, bool use_flip_axis);
		virtual ~BlendTexture() override;
		virtual Rgba getColor(const Point3 &p, const MipMapParams *mipmap_params = nullptr) const override;
		virtual float getFloat(const Point3 &p, const MipMapParams *mipmap_params = nullptr) const override;

		ProgressionType progression_type_ = Linear;
		bool use_flip_axis_ = false;
};

END_YAFRAY

#endif // YAFARAY_BASICTEX_H
