#pragma once
/****************************************************************************
 * 		curveUtils.cc: Curve interpolation utils
 *      This is part of the yafaray package
 *      Copyright (C) 2009 Rodrigo Placencia
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

#ifndef YAFARAY_CURVEUTILS_H
#define YAFARAY_CURVEUTILS_H

#include <yafray_constants.h>

BEGIN_YAFRAY

//////////////////////////////////////////////////////////////////////////////
// irregularCurve declaration & definition
//////////////////////////////////////////////////////////////////////////////

class IrregularCurve final
{
	public:
		IrregularCurve(const float *datay, const float *datax, int n);
		IrregularCurve(const float *datay, int n);
		~IrregularCurve();
		float getSample(float wl) const;
		float operator()(float x) const {return getSample(x);};
		void addSample(float data);

	private:
		float *c_1_ = nullptr;
		float *c_2_ = nullptr;
		int size_;
		int index_;
};

IrregularCurve::IrregularCurve(const float *datay, const float *datax, int n): c_1_(nullptr), c_2_(nullptr), size_(n), index_(0)
{
	c_1_ = new float[n];
	c_2_ = new float[n];
	for(int i = 0; i < n; i++)
	{
		c_1_[i] = datax[i];
		c_2_[i] = datay[i];
	}
}

IrregularCurve::IrregularCurve(const float *datay, int n): c_1_(nullptr), c_2_(nullptr), size_(n), index_(0)
{
	c_1_ = new float[n];
	c_2_ = new float[n];
	for(int i = 0; i < n; i++) c_2_[i] = datay[i];
}

IrregularCurve::~IrregularCurve()
{
	if(c_1_) { delete [] c_1_; c_1_ = nullptr; }
	if(c_2_) { delete [] c_2_; c_2_ = nullptr; }
}

float IrregularCurve::getSample(float x) const
{
	if(x < c_1_[0] || x > c_1_[size_ - 1]) return 0.0;
	int zero = 0;

	for(int i = 0; i < size_; i++)
	{
		if(c_1_[i] == x) return c_2_[i];
		else if(c_1_[i] <= x && c_1_[i + 1] > x)
		{
			zero = i;
			break;
		}
	}

	float y = x - c_1_[zero];
	y *= (c_2_[zero + 1] - c_2_[zero]) / (c_1_[zero + 1] - c_1_[zero]);
	y += c_2_[zero];
	return y;
}

void IrregularCurve::addSample(float data)
{
	if(index_ < size_) c_1_[index_++] = data;
}

//////////////////////////////////////////////////////////////////////////////
// regularCurve declaration & definition
//////////////////////////////////////////////////////////////////////////////

class RegularCurve final
{
	public:
		RegularCurve(const float *data, float begin_r, float end_r, int n);
		RegularCurve(float begin_r, float end_r, int n);
		~RegularCurve();
		float getSample(float x) const;
		float operator()(float x) const {return getSample(x);};
		void addSample(float data);

	private:
		float *c_ = nullptr;
		float end_r_;
		float begin_r_;
		float step_;
		int size_;
		int index_;
};

RegularCurve::RegularCurve(const float *data, float begin_r, float end_r, int n):
		c_(nullptr), end_r_(begin_r), begin_r_(end_r), step_(0.0), size_(n), index_(0)
{
	c_ = new float[n];
	for(int i = 0; i < n; i++) c_[i] = data[i];
	step_ = n / (begin_r_ - end_r_);
}

RegularCurve::RegularCurve(float begin_r, float end_r, int n) : c_(nullptr), end_r_(begin_r), begin_r_(end_r), step_(0.0), size_(n), index_(0)
{
	c_ = new float[n];
	step_ = n / (begin_r_ - end_r_);
}

RegularCurve::~RegularCurve()
{
	if(c_) { delete [] c_; c_ = nullptr; }
}

float RegularCurve::getSample(float x) const
{
	if(x < end_r_ || x > begin_r_) return 0.0;
	float med, x_0, x_1, y;
	int y_0, y_1;

	med = (x - end_r_) * step_;
	y_0 = static_cast<int>(floor(med));
	y_1 = static_cast<int>(ceil(med));

	if(y_0 == y_1) return c_[y_0];

	x_0 = (y_0 / step_) + end_r_;
	x_1 = (y_1 / step_) + end_r_;

	y = x - x_0;
	y *= (c_[y_1] - c_[y_0]) / (x_1 - x_0);
	y += c_[y_0];

	return y;
}

void RegularCurve::addSample(float data)
{
	if(index_ < size_) c_[index_++] = data;
}

END_YAFRAY
#endif
