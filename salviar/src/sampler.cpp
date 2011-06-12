#include "../include/sampler.h"

#include "../include/surface.h"
#include "../include/texture.h"

#include <eflib/include/platform/ext_intrinsics.h>

BEGIN_NS_SALVIAR()

using namespace eflib;
using namespace std;

namespace addresser
{
	struct wrap
	{
		static float do_coordf(float coord, int size)
		{
			return (coord - fast_floor(coord)) * size - 0.5f;
		}

		static int do_coordi_point_1d(int coord, int size)
		{
			return (size * 8192 + coord) % size;
		}
		static int4 do_coordi_point_2d(const vec4& coord, const int4& size)
		{
#ifndef EFLIB_NO_SIMD
			__m128 mfcoord = _mm_loadu_ps(&coord.x);
			__m128i misize = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&size.x));

			mfcoord = _mm_sub_ps(mfcoord, _mm_cvtepi32_ps(_mm_cvttps_epi32(mfcoord)));
			__m128 mfsize = _mm_cvtepi32_ps(misize);
			mfcoord = _mm_mul_ps(mfsize, mfcoord);

			__m128 mfcoord_ipart = _mm_cvtepi32_ps(_mm_cvttps_epi32(mfcoord));
			__m128 mask = _mm_cmpgt_ps(mfcoord_ipart, mfcoord);		// if it increased (i.e. if it was negative...)
			mask = _mm_and_ps(mask, _mm_set1_ps(1.0f));				// ...without a conditional branch...
			mfcoord_ipart = _mm_sub_ps(mfcoord_ipart, mask);

			mfcoord = _mm_add_ps(mfcoord_ipart, _mm_mul_ps(mfsize, _mm_set1_ps(8192.0f)));
			__m128 mfdiv = _mm_cvtepi32_ps(_mm_cvttps_epi32(_mm_div_ps(mfcoord, mfsize)));
			__m128i tmp = _mm_cvttps_epi32(_mm_sub_ps(mfcoord, _mm_mul_ps(mfdiv, mfsize)));
			int4 ret;
			_mm_storeu_si128(reinterpret_cast<__m128i*>(&ret.x), tmp);
			return ret;
#else
			vec4 o_coord = (coord - vec4(fast_floor(coord.x), fast_floor(coord.y), fast_floor(coord.z), fast_floor(coord.w)))
				* vec4(static_cast<float>(size.x), static_cast<float>(size.y), static_cast<float>(size.z), static_cast<float>(size.w));
			int4 coord_ipart = int4(fast_floori(o_coord.x), fast_floori(o_coord.y), 0, 0);

			return int4((size.x * 8192 + coord_ipart.x) % size.x,
				(size.y * 8192 + coord_ipart.y) % size.y,
				0, 0);
#endif
		}

		static void do_coordi_linear_2d(int4& low, int4& up, vec4& frac, const vec4& coord, const int4& size)
		{
#ifndef EFLIB_NO_SIMD
			__m128 mfcoord = _mm_loadu_ps(&coord.x);
			__m128i misize = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&size.x));

			__m128 mfcoord0 = _mm_sub_ps(mfcoord, _mm_cvtepi32_ps(_mm_cvttps_epi32(mfcoord)));
			misize = _mm_unpacklo_epi64(misize, misize);
			__m128 mfsize = _mm_cvtepi32_ps(misize);
			mfcoord0 = _mm_mul_ps(mfsize, mfcoord0);
			const __m128 mhalf = _mm_set1_ps(0.5f);
			mfcoord0 = _mm_sub_ps(mfcoord0, mhalf);
			
			__m128 mfcoord_ipart = _mm_cvtepi32_ps(_mm_cvttps_epi32(mfcoord0));
			__m128 mask = _mm_cmpgt_ps(mfcoord_ipart, mfcoord0);	// if it increased (i.e. if it was negative...)
			mask = _mm_and_ps(mask, _mm_set1_ps(1.0f));				// ...without a conditional branch...
			mfcoord_ipart = _mm_sub_ps(mfcoord_ipart, mask);
			__m128 mffrac = _mm_sub_ps(mfcoord0, mfcoord_ipart);
			_mm_storeu_ps(&frac.x, mffrac);

			__m128 mfcoord01 = _mm_movelh_ps(mfcoord_ipart, mfcoord_ipart);
			mfcoord01 = _mm_add_ps(mfcoord01, _mm_set_ps(1.0f, 1.0f, 0.0f, 0.0f));
			mfcoord01 = _mm_add_ps(mfcoord01, _mm_mul_ps(mfsize, _mm_set1_ps(8192.0f)));
			__m128 mfdiv = _mm_cvtepi32_ps(_mm_cvttps_epi32(_mm_div_ps(mfcoord01, mfsize)));
			__m128i tmp = _mm_cvttps_epi32(_mm_sub_ps(mfcoord01, _mm_mul_ps(mfdiv, mfsize)));
			_mm_storeu_si128(reinterpret_cast<__m128i*>(&low.x), tmp);
			tmp = _mm_unpackhi_epi64(tmp, tmp);
			_mm_storeu_si128(reinterpret_cast<__m128i*>(&up.x), tmp);
#else
			vec4 o_coord = (coord - vec4(fast_floor(coord.x), fast_floor(coord.y), fast_floor(coord.z), fast_floor(coord.w)))
				* vec4(static_cast<float>(size.x), static_cast<float>(size.y), static_cast<float>(size.z), static_cast<float>(size.w)) - 0.5f;
			int4 coord_ipart = int4(fast_floori(o_coord.x), fast_floori(o_coord.y), 0, 0);
			frac = o_coord - vec4(static_cast<float>(coord_ipart.x), static_cast<float>(coord_ipart.y), 0, 0);

			low = int4((size.x * 8192 + coord_ipart.x) % size.x,
				(size.y * 8192 + coord_ipart.y) % size.y,
				0, 0);
			up = int4((size.x * 8192 + coord_ipart.x + 1) % size.x,
				(size.y * 8192 + coord_ipart.y + 1) % size.y,
				0, 0);
#endif
		}
	};

	struct mirror
	{
		static float do_coordf(float coord, int size)
		{
			int selection_coord = fast_floori(coord);
			return 
				(selection_coord & 1 
				? 1 + selection_coord - coord
				: coord - selection_coord) * size - 0.5f;
		}

		static int do_coordi_point_1d(int coord, int size)
		{
			return eflib::clamp(coord, 0, size - 1);
		}
		static int4 do_coordi_point_2d(const vec4& coord, const int4& size)
		{
			int selection_coord_x = fast_floori(coord.x);
			int selection_coord_y = fast_floori(coord.y);
			vec4 o_coord((selection_coord_x & 1 
				? 1 + selection_coord_x - coord.x
				: coord.x - selection_coord_x) * size.x,
				(selection_coord_y & 1 
				? 1 + selection_coord_y - coord.y
				: coord.y - selection_coord_y) * size.y,
				0, 0);

#ifndef EFLIB_NO_SIMD
			__m128 mfcoord = _mm_loadu_ps(&o_coord.x);
			__m128i misize = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&size));

			__m128 mfcoord_ipart = _mm_cvtepi32_ps(_mm_cvttps_epi32(mfcoord));
			__m128 mask = _mm_cmpgt_ps(mfcoord_ipart, mfcoord);		// if it increased (i.e. if it was negative...)
			mask = _mm_and_ps(mask, _mm_set1_ps(1.0f));				// ...without a conditional branch...
			mfcoord_ipart = _mm_sub_ps(mfcoord_ipart, mask);

			__m128 mfsize = _mm_cvtepi32_ps(misize);
			mfsize = _mm_sub_ps(mfsize, _mm_set1_ps(1.0f));
			__m128i tmp = _mm_cvttps_epi32(_mm_min_ps(_mm_max_ps(mfcoord_ipart, _mm_setzero_ps()), mfsize));
			int4 ret;
			_mm_storeu_si128(reinterpret_cast<__m128i*>(&ret.x), tmp);
			return ret;
#else
			int4 coord_ipart = int4(fast_floori(o_coord.x), fast_floori(o_coord.y), 0, 0);

			return int4(eflib::clamp(coord_ipart.x, 0, size.x - 1),
				eflib::clamp(coord_ipart.y, 0, size.y - 1),
				0, 0);
#endif
		}

		static void do_coordi_linear_2d(int4& low, int4& up, vec4& frac, const vec4& coord, const int4& size)
		{
			int selection_coord_x = fast_floori(coord.x);
			int selection_coord_y = fast_floori(coord.y);
			vec4 o_coord((selection_coord_x & 1 
				? 1 + selection_coord_x - coord.x
				: coord.x - selection_coord_x) * size.x - 0.5f,
				(selection_coord_y & 1 
				? 1 + selection_coord_y - coord.y
				: coord.y - selection_coord_y) * size.y - 0.5f,
				0, 0);

#ifndef EFLIB_NO_SIMD
			__m128 mfcoord0 = _mm_loadu_ps(&o_coord.x);
			__m128i misize = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&size));

			__m128 mfcoord_ipart = _mm_cvtepi32_ps(_mm_cvttps_epi32(mfcoord0));
			__m128 mask = _mm_cmpgt_ps(mfcoord_ipart, mfcoord0);	// if it increased (i.e. if it was negative...)
			mask = _mm_and_ps(mask, _mm_set1_ps(1.0f));				// ...without a conditional branch...
			mfcoord_ipart = _mm_sub_ps(mfcoord_ipart, mask);
			__m128 mffrac = _mm_sub_ps(mfcoord0, mfcoord_ipart);
			_mm_storeu_ps(&frac.x, mffrac);

			__m128 mfcoord01 = _mm_movelh_ps(mfcoord_ipart, mfcoord_ipart);
			mfcoord01 = _mm_add_ps(mfcoord01, _mm_set_ps(1.0f, 1.0f, 0.0f, 0.0f));
			__m128 mfsize = _mm_cvtepi32_ps(misize);
			mfsize = _mm_movelh_ps(mfsize, mfsize);
			mfsize = _mm_sub_ps(mfsize, _mm_set1_ps(1.0f));
			__m128i tmp = _mm_cvttps_epi32(_mm_min_ps(_mm_max_ps(mfcoord01, _mm_setzero_ps()), mfsize));
			_mm_storeu_si128(reinterpret_cast<__m128i*>(&low.x), tmp);
			tmp = _mm_unpackhi_epi64(tmp, tmp);
			_mm_storeu_si128(reinterpret_cast<__m128i*>(&up.x), tmp);
#else
			int4 coord_ipart = int4(fast_floori(o_coord.x), fast_floori(o_coord.y), 0, 0);
			frac = o_coord - vec4(static_cast<float>(coord_ipart.x), static_cast<float>(coord_ipart.y), 0, 0);

			low = int4(eflib::clamp(coord_ipart.x, 0, size.x - 1),
				eflib::clamp(coord_ipart.y, 0, size.y - 1),
				0, 0);
			up = int4(eflib::clamp(coord_ipart.x + 1, 0, size.x - 1),
				eflib::clamp(coord_ipart.y + 1, 0, size.y - 1),
				0, 0);
#endif
		}
	};

	struct clamp
	{
		static float do_coordf(float coord, int size)
		{
			return eflib::clamp(coord * size, 0.5f, size - 0.5f) - 0.5f;
		}

		static int do_coordi_point_1d(int coord, int size)
		{
			return eflib::clamp(coord, 0, size - 1);
		}
		static int4 do_coordi_point_2d(const vec4& coord, const int4& size)
		{
#ifndef EFLIB_NO_SIMD
			__m128 mfcoord = _mm_loadu_ps(&coord.x);
			__m128i misize = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&size.x));

			misize = _mm_unpacklo_epi64(misize, misize);
			__m128 mfsize = _mm_cvtepi32_ps(misize);
			const __m128 mhalf = _mm_set1_ps(0.5f);
			__m128 mfcoord0 = _mm_mul_ps(mfcoord, mfsize);
			mfcoord0 = _mm_max_ps(mfcoord0, mhalf);
			mfcoord0 = _mm_min_ps(mfcoord0, _mm_sub_ps(mfsize, mhalf));

			__m128 mfcoord_ipart = _mm_cvtepi32_ps(_mm_cvttps_epi32(mfcoord));
			__m128 mask = _mm_cmpgt_ps(mfcoord_ipart, mfcoord);		// if it increased (i.e. if it was negative...)
			mask = _mm_and_ps(mask, _mm_set1_ps(1.0f));				// ...without a conditional branch...
			mfcoord_ipart = _mm_sub_ps(mfcoord_ipart, mask);

			mfsize = _mm_sub_ps(mfsize, _mm_set1_ps(1));
			__m128i tmp = _mm_cvttps_epi32(_mm_min_ps(_mm_max_ps(mfcoord_ipart, _mm_setzero_ps()), mfsize));
			int4 ret;
			_mm_storeu_si128(reinterpret_cast<__m128i*>(&ret.x), tmp);
			return ret;
#else
			vec4 o_coord(eflib::clamp(coord.x * size.x, 0.5f, size.x - 0.5f),
				eflib::clamp(coord.y * size.y, 0.5f, size.y - 0.5f),
				0, 0);
			int4 coord_ipart = int4(fast_floori(o_coord.x), fast_floori(o_coord.y), 0, 0);

			return int4(eflib::clamp(coord_ipart.x, 0, size.x - 1),
				eflib::clamp(coord_ipart.y, 0, size.y - 1),
				0, 0);
#endif
		}

		static void do_coordi_linear_2d(int4& low, int4& up, vec4& frac, const vec4& coord, const int4& size)
		{
#ifndef EFLIB_NO_SIMD
			__m128 mfcoord = _mm_loadu_ps(&coord.x);
			__m128i misize = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&size.x));

			misize = _mm_unpacklo_epi64(misize, misize);
			__m128 mfsize = _mm_cvtepi32_ps(misize);
			const __m128 mhalf = _mm_set1_ps(0.5f);
			__m128 mfcoord0 = _mm_mul_ps(mfcoord, mfsize);
			mfcoord0 = _mm_max_ps(mfcoord0, mhalf);
			mfcoord0 = _mm_min_ps(mfcoord0, _mm_sub_ps(mfsize, mhalf));
			mfcoord0 = _mm_sub_ps(mfcoord0, mhalf);

			__m128 mfcoord_ipart = _mm_cvtepi32_ps(_mm_cvttps_epi32(mfcoord0));
			__m128 mask = _mm_cmpgt_ps(mfcoord_ipart, mfcoord0);	// if it increased (i.e. if it was negative...)
			mask = _mm_and_ps(mask, _mm_set1_ps(1.0f));				// ...without a conditional branch...
			mfcoord_ipart = _mm_sub_ps(mfcoord_ipart, mask);
			__m128 mffrac = _mm_sub_ps(mfcoord0, mfcoord_ipart);
			_mm_storeu_ps(&frac.x, mffrac);

			__m128 mfcoord01 = _mm_movelh_ps(mfcoord_ipart, mfcoord_ipart);
			mfcoord01 = _mm_add_ps(mfcoord01, _mm_set_ps(1.0f, 1.0f, 0.0f, 0.0f));
			mfsize = _mm_sub_ps(mfsize, _mm_set1_ps(1));
			__m128i tmp = _mm_cvttps_epi32(_mm_min_ps(_mm_max_ps(mfcoord01, _mm_setzero_ps()), mfsize));
			_mm_storeu_si128(reinterpret_cast<__m128i*>(&low.x), tmp);
			tmp = _mm_unpackhi_epi64(tmp, tmp);
			_mm_storeu_si128(reinterpret_cast<__m128i*>(&up.x), tmp);
#else
			vec4 o_coord(eflib::clamp(coord.x * size.x, 0.5f, size.x - 0.5f) - 0.5f,
				eflib::clamp(coord.y * size.y, 0.5f, size.y - 0.5f) - 0.5f,
				0, 0);
			int4 coord_ipart = int4(fast_floori(o_coord.x), fast_floori(o_coord.y), 0, 0);
			frac = o_coord - vec4(static_cast<float>(coord_ipart.x), static_cast<float>(coord_ipart.y), 0, 0);

			low = int4(eflib::clamp(coord_ipart.x, 0, size.x - 1),
				eflib::clamp(coord_ipart.y, 0, size.y - 1),
				0, 0);
			up = int4(eflib::clamp(coord_ipart.x + 1, 0, size.x - 1),
				eflib::clamp(coord_ipart.y + 1, 0, size.y - 1),
				0, 0);
#endif
		}
	};

	struct border
	{
		static float do_coordf(float coord, int size)
		{
			return eflib::clamp(coord * size, -0.5f, size + 0.5f) - 0.5f;
		}

		static int do_coordi_point_1d(int coord, int size)
		{
			return coord >= size ? -1 : coord;
		}
		static int4 do_coordi_point_2d(const vec4& coord, const int4& size)
		{
#ifndef EFLIB_NO_SIMD
			__m128 mfcoord = _mm_loadu_ps(&coord.x);
			__m128 mfsize = _mm_cvtepi32_ps(_mm_loadu_si128(reinterpret_cast<const __m128i*>(&size.x)));

			const __m128 mneghalf = _mm_set1_ps(-0.5f);
			__m128 tmp = _mm_mul_ps(mfcoord, mfsize);
			tmp = _mm_max_ps(tmp, mneghalf);
			mfcoord = _mm_min_ps(tmp, _mm_sub_ps(mfsize, mneghalf));

			__m128 mfcoord_ipart = _mm_cvtepi32_ps(_mm_cvttps_epi32(mfcoord));
			__m128 mask = _mm_cmpgt_ps(mfcoord_ipart, mfcoord);		// if it increased (i.e. if it was negative...)
			mask = _mm_and_ps(mask, _mm_set1_ps(1.0f));				// ...without a conditional branch...
			mfcoord_ipart = _mm_sub_ps(mfcoord_ipart, mask);
			int4 coord_ipart;
			_mm_storeu_si128(reinterpret_cast<__m128i*>(&coord_ipart.x), _mm_cvttps_epi32(mfcoord_ipart));
#else
			vec4 o_coord(eflib::clamp(coord.x * size.x, -0.5f, size.x + 0.5f),
				eflib::clamp(coord.y * size.y, -0.5f, size.y + 0.5f),
				0, 0);

			int4 coord_ipart = int4(fast_floori(o_coord.x), fast_floori(o_coord.y), 0, 0);
#endif

			return int4(coord_ipart.x >= size.x ? -1 : coord_ipart.x,
				coord_ipart.y >= size.y ? -1 : coord_ipart.y,
				0, 0);
		}

		static void do_coordi_linear_2d(int4& low, int4& up, vec4& frac, const vec4& coord, const int4& size)
		{
#ifndef EFLIB_NO_SIMD
			__m128 mfcoord = _mm_loadu_ps(&coord.x);
			__m128 mfsize = _mm_cvtepi32_ps(_mm_loadu_si128(reinterpret_cast<const __m128i*>(&size.x)));

			const __m128 mneghalf = _mm_set1_ps(-0.5f);
			__m128 tmp = _mm_mul_ps(mfcoord, mfsize);
			tmp = _mm_max_ps(tmp, mneghalf);
			tmp = _mm_min_ps(tmp, _mm_sub_ps(mfsize, mneghalf));
			__m128 mfcoord0 = _mm_add_ps(tmp, mneghalf);

			__m128 mfcoord_ipart = _mm_cvtepi32_ps(_mm_cvttps_epi32(mfcoord0));
			__m128 mask = _mm_cmpgt_ps(mfcoord_ipart, mfcoord0);	// if it increased (i.e. if it was negative...)
			mask = _mm_and_ps(mask, _mm_set1_ps(1.0f));				// ...without a conditional branch...
			mfcoord_ipart = _mm_sub_ps(mfcoord_ipart, mask);
			__m128 mffrac = _mm_sub_ps(mfcoord0, mfcoord_ipart);
			_mm_storeu_ps(&frac.x, mffrac);
			int4 coord_ipart;
			_mm_storeu_si128(reinterpret_cast<__m128i*>(&coord_ipart.x), _mm_cvttps_epi32(mfcoord_ipart));
#else
			vec4 o_coord(eflib::clamp(coord.x * size.x, -0.5f, size.x + 0.5f) - 0.5f,
				eflib::clamp(coord.y * size.y, -0.5f, size.y + 0.5f) - 0.5f,
				0, 0);
			int4 coord_ipart = int4(fast_floori(o_coord.x), fast_floori(o_coord.y), 0, 0);
			frac = o_coord - vec4(static_cast<float>(coord_ipart.x), static_cast<float>(coord_ipart.y), 0, 0);
#endif

			low = int4(coord_ipart.x >= size.x ? -1 : coord_ipart.x,
				coord_ipart.y >= size.y ? -1 : coord_ipart.y,
				0, 0);
			up = int4(coord_ipart.x + 1 >= size.x ? -1 : coord_ipart.x + 1,
				coord_ipart.y + 1 >= size.y ? -1 : coord_ipart.y + 1,
				0, 0);
		}
	};
};

namespace coord_calculator
{
	template <typename addresser_type>
	int point_cc(float coord, int size)
	{
		float o_coord = addresser_type::do_coordf(coord, size);
		int coord_ipart = fast_floori(o_coord + 0.5f);
		return addresser_type::do_coordi_point_1d(coord_ipart, size);
	}

	template <typename addresser_type>
	void linear_cc(int& low, int& up, float& frac, float coord, int size)
	{
		float o_coord = addresser_type::do_coordf(coord, size);
		int coord_ipart = fast_floori(o_coord);
		low = addresser_type::do_coordi_point_1d(coord_ipart, size);
		up = addresser_type::do_coordi_point_1d(coord_ipart + 1, size);
		frac = o_coord - coord_ipart;
	}

	template <typename addresser_type>
	int4 point_cc(const vec4& coord, const int4& size)
	{
		return addresser_type::do_coordi_point_2d(coord, size);
	}

	template <typename addresser_type>
	void linear_cc(int4& low, int4& up, vec4& frac, const vec4& coord, const int4& size)
	{
		addresser_type::do_coordi_linear_2d(low, up, frac, coord, size);
	}
};

namespace surface_sampler
{
	template <typename addresser_type_u, typename addresser_type_v>
	struct point
	{
		static color_rgba32f op(const surface& surf, float x, float y, size_t sample, const color_rgba32f& border_color)
		{
			int ix = coord_calculator::point_cc<addresser_type_u>(x, int(surf.get_width()));
			int iy = coord_calculator::point_cc<addresser_type_v>(y, int(surf.get_height()));

			if(ix < 0 || iy < 0) return border_color;
			return surf.get_texel(ix, iy, sample);
		}
	};

	template <typename addresser_type_u, typename addresser_type_v>
	struct linear
	{
		static color_rgba32f op(const surface& surf, float x, float y, size_t sample, const color_rgba32f& /*border_color*/)
		{
			int xpos0, ypos0, xpos1, ypos1;
			float tx, ty;
			coord_calculator::linear_cc<addresser_type_u>(xpos0, xpos1, tx, x, int(surf.get_width()));
			coord_calculator::linear_cc<addresser_type_v>(ypos0, ypos1, ty, y, int(surf.get_height()));

			return surf.get_texel(xpos0, ypos0, xpos1, ypos1, tx, ty, sample);
		}
	};

	template <typename addresser_type_uv>
	struct point<addresser_type_uv, addresser_type_uv>
	{
		static color_rgba32f op(const surface& surf, float x, float y, size_t sample, const color_rgba32f& border_color)
		{
			int4 ixy = coord_calculator::point_cc<addresser_type_uv>(vec4(x, y, 0, 0),
				int4(static_cast<int>(surf.get_width()), static_cast<int>(surf.get_height()), 0, 0));

			if(ixy.x < 0 || ixy.y < 0) return border_color;
			return surf.get_texel(ixy.x, ixy.y, sample);
		}
	};

	template <typename addresser_type_uv>
	struct linear<addresser_type_uv, addresser_type_uv>
	{
		static color_rgba32f op(const surface& surf, float x, float y, size_t sample, const color_rgba32f& /*border_color*/)
		{
			int4 pos0, pos1;
			vec4 t;
			coord_calculator::linear_cc<addresser_type_uv>(pos0, pos1, t, vec4(x, y, 0, 0),
				int4(static_cast<int>(surf.get_width()), static_cast<int>(surf.get_height()), 0, 0));

			return surf.get_texel(pos0.x, pos0.y, pos1.x, pos1.y, t.x, t.y, sample);
		}
	};

	const sampler::filter_op_type filter_table[filter_type_count][address_mode_count][address_mode_count] = 
	{
		{
			{
				point<addresser::wrap, addresser::wrap>::op,
				point<addresser::wrap, addresser::mirror>::op,
				point<addresser::wrap, addresser::clamp>::op,
				point<addresser::wrap, addresser::border>::op
			},
			{
				point<addresser::mirror, addresser::wrap>::op,
				point<addresser::mirror, addresser::mirror>::op,
				point<addresser::mirror, addresser::clamp>::op,
				point<addresser::mirror, addresser::border>::op
			},
			{
				point<addresser::clamp, addresser::wrap>::op,
				point<addresser::clamp, addresser::mirror>::op,
				point<addresser::clamp, addresser::clamp>::op,
				point<addresser::clamp, addresser::border>::op
			},
			{
				point<addresser::border, addresser::wrap>::op,
				point<addresser::border, addresser::mirror>::op,
				point<addresser::border, addresser::clamp>::op,
				point<addresser::border, addresser::border>::op
			}
		},
		{
			{
				linear<addresser::wrap, addresser::wrap>::op,
				linear<addresser::wrap, addresser::mirror>::op,
				linear<addresser::wrap, addresser::clamp>::op,
				linear<addresser::wrap, addresser::border>::op
			},
			{
				linear<addresser::mirror, addresser::wrap>::op,
				linear<addresser::mirror, addresser::mirror>::op,
				linear<addresser::mirror, addresser::clamp>::op,
				linear<addresser::mirror, addresser::border>::op
			},
			{
				linear<addresser::clamp, addresser::wrap>::op,
				linear<addresser::clamp, addresser::mirror>::op,
				linear<addresser::clamp, addresser::clamp>::op,
				linear<addresser::clamp, addresser::border>::op
			},
			{
				linear<addresser::border, addresser::wrap>::op,
				linear<addresser::border, addresser::mirror>::op,
				linear<addresser::border, addresser::clamp>::op,
				linear<addresser::border, addresser::border>::op
			}
		}
	};
}

float sampler::calc_lod(const vec4& attribute, const int4& size, const vec4& ddx, const vec4& ddy, float inv_x_w, float inv_y_w, float inv_w, float bias) const
{
#ifndef EFLIB_NO_SIMD
	static const union
	{
		int maski;
		float maskf;
	} MASK = { 0x7FFFFFFF };
	static const __m128 ABS_MASK = _mm_set1_ps(MASK.maskf);

	__m128 mattr = _mm_loadu_ps(&attribute.x);
	__m128 tmp = _mm_mul_ps(mattr, _mm_set1_ps(inv_w));
	__m128 mddx2 = _mm_sub_ps(_mm_mul_ps(_mm_add_ps(mattr, _mm_loadu_ps(&ddx.x)), _mm_set1_ps(inv_x_w)), tmp);
	__m128 mddy2 = _mm_sub_ps(_mm_mul_ps(_mm_add_ps(mattr, _mm_loadu_ps(&ddy.x)), _mm_set1_ps(inv_y_w)), tmp);
	mddx2 = _mm_and_ps(mddx2, ABS_MASK);
	mddy2 = _mm_and_ps(mddy2, ABS_MASK);
	tmp = _mm_max_ps(mddx2, mddy2);
	tmp = _mm_mul_ps(tmp, _mm_cvtepi32_ps(_mm_loadu_si128(reinterpret_cast<const __m128i*>(&size.x))));
	__m128 tmp2 = _mm_shuffle_ps(tmp, tmp, _MM_SHUFFLE(2, 2, 2, 2));
	tmp = _mm_max_ps(tmp, tmp2);
	tmp2 = _mm_shuffle_ps(tmp, tmp, _MM_SHUFFLE(1, 1, 1, 1));
	tmp = _mm_max_ss(tmp, tmp2);
	__m128 mrho = _mm_max_ss(tmp, _mm_set_ss(0.000001f));

	// log 2
	__m128i mx = eflib_mm_castps_si128(mrho);
	__m128 mlog2 = _mm_cvtepi32_ps(_mm_sub_epi32(_mm_and_si128(_mm_srli_epi32(mx, 23), _mm_set1_epi32(255)), _mm_set1_epi32(128)));
	mx = _mm_and_si128(mx, _mm_set1_epi32(0x007FFFFF));
	mx = _mm_or_si128(mx, _mm_set1_epi32(0x3F800000));
	tmp = eflib_mm_castsi128_ps(mx);
	tmp = _mm_sub_ss(_mm_mul_ss(_mm_add_ss(_mm_mul_ss(tmp, _mm_set_ss(-1.0f / 3)), _mm_set_ss(2)), tmp), _mm_set_ss(2.0f / 3));
	__m128 mlambda = _mm_add_ss(tmp, mlog2);

	__m128 mlod = _mm_add_ss(mlambda, _mm_set_ss(bias));

	float lod;
	_mm_store_ss(&lod, mlod);
	return lod;
#else
	eflib::vec4 ddx2 = (attribute + ddx) * inv_x_w - attribute * inv_w;
	eflib::vec4 ddy2 = (attribute + ddy) * inv_y_w - attribute * inv_w;

	float rho, lambda;

	vec4 maxD(
		max(abs(ddx2.x), abs(ddy2.x)), 
		max(abs(ddx2.y), abs(ddy2.y)), 
		max(abs(ddx2.z), abs(ddy2.z)), 
		0.0f);
	maxD *= vec4(static_cast<float>(size.x), static_cast<float>(size.y), static_cast<float>(size.z), 0);

	rho = max(max(maxD.x, maxD.y), maxD.z);
	if(rho == 0.0f) rho = 0.000001f;
	lambda = fast_log2(rho);
	return lambda + bias;
#endif
}

color_rgba32f sampler::sample_surface(
	const surface& surf,
	float x, float y, size_t sample, 
	sampler_state ss) const
{
	return filters_[ss](
			surf, 
			x, y, sample,
			desc_.border_color
			);
}

sampler::sampler(const sampler_desc& desc)
	: desc_(desc)
{
	filters_[sampler_state_min] = surface_sampler::filter_table[desc_.min_filter][desc_.addr_mode_u][desc.addr_mode_v];
	filters_[sampler_state_mag] = surface_sampler::filter_table[desc_.mag_filter][desc_.addr_mode_u][desc.addr_mode_v];
	filters_[sampler_state_mip] = surface_sampler::filter_table[desc_.mip_filter][desc_.addr_mode_u][desc.addr_mode_v];
}

color_rgba32f sampler::sample_impl(const texture *tex , float coordx, float coordy, size_t sample, float miplevel) const
{
	bool is_mag = true;

	if(desc_.mip_filter == filter_point) {
		is_mag = (miplevel < 0.5f);
	} else {
		is_mag = (miplevel < 0);
	}

	//�Ŵ�
	if(is_mag){
		return sample_surface(tex->get_surface(tex->get_max_lod()), coordx, coordy, sample, sampler_state_mag);
	}

	if(desc_.mip_filter == filter_point){
		size_t ml = fast_floori(miplevel + 0.5f);
		ml = clamp(ml, tex->get_max_lod(), tex->get_min_lod());

		return sample_surface(tex->get_surface(ml), coordx, coordy, sample, sampler_state_min);
	}

	if(desc_.mip_filter == filter_linear){
		size_t low = fast_floori(miplevel);
		size_t up = low + 1;

		float frac = miplevel - low;

		low = clamp(low, tex->get_max_lod(), tex->get_min_lod());
		up = clamp(up, tex->get_max_lod(), tex->get_min_lod());

		color_rgba32f c0 = sample_surface(tex->get_surface(low), coordx, coordy, sample, sampler_state_min);
		color_rgba32f c1 = sample_surface(tex->get_surface(up), coordx, coordy, sample, sampler_state_min);
	
		return lerp(c0, c1, frac);
	}

	EFLIB_ASSERT(false, "�����˴����mip filters����");
	return desc_.border_color;
}

color_rgba32f sampler::sample_impl(const texture *tex , 
								 float coordx, float coordy, size_t sample,
								 const vec4& ddx, const vec4& ddy,
								 float inv_x_w, float inv_y_w, float inv_w, float lod_bias) const
{
	return sample_2d_impl(tex,
		vec4(coordx, coordy, 0.0f, 1.0f / inv_w), sample,
		ddx, ddy, inv_x_w, inv_y_w, inv_w, lod_bias
		);
}

color_rgba32f sampler::sample_2d_impl(const texture *tex , 
								 const vec4& coord, size_t sample,
								 const vec4& ddx, const vec4& ddy,
								 float inv_x_w, float inv_y_w, float inv_w, float lod_bias) const
{
	float q = inv_w == 0 ? 1.0f : 1.0f / inv_w;
	vec4 origin_coord(coord * q);
	int4 size(static_cast<int>(tex->get_width(0)), static_cast<int>(tex->get_height(0)),
		static_cast<int>(tex->get_depth(0)), 0);

	float lod = calc_lod(origin_coord, size, ddx, ddy, inv_x_w, inv_y_w, inv_w, lod_bias);
	return sample_impl(tex, coord.x, coord.y, sample, lod);
}


color_rgba32f sampler::sample(float coordx, float coordy, float miplevel) const
{
	return sample_impl(ptex_, coordx, coordy, 0, miplevel);
}

color_rgba32f sampler::sample(
					 float coordx, float coordy, 
					 const eflib::vec4& ddx, const eflib::vec4& ddy, 
					 float inv_x_w, float inv_y_w, float inv_w, float lod_bias) const
{
	return sample_impl(ptex_, coordx, coordy, 0, ddx, ddy, inv_x_w, inv_y_w, inv_w, lod_bias);
}


color_rgba32f sampler::sample_2d(
						const eflib::vec4& coord,
						const eflib::vec4& ddx, const eflib::vec4& ddy,
						float inv_x_w, float inv_y_w, float inv_w, float lod_bias) const
{
	return sample_2d_impl(ptex_, coord, 0, ddx, ddy, inv_x_w, inv_y_w, inv_w, lod_bias);
}


color_rgba32f sampler::sample_cube(
	float coordx, float coordy, float coordz,
	float miplevel
	) const
{
	cubemap_faces major_dir;
	float s, t, m;

	float x = coordx;
	float y = coordy;
	float z = coordz;

	float ax = abs(x);
	float ay = abs(y);
	float az = abs(z);

	if(ax > ay && ax > az)
	{
		// x max
		m = ax;
		if(x > 0){
			//+x
			s = 0.5f * (z / m + 1.0f);
			t = 0.5f * (y / m + 1.0f);
			major_dir = cubemap_face_positive_x;
		} else {
			//-x

			s = 0.5f * (-z / m + 1.0f);
			t = 0.5f * (y / m + 1.0f);
			major_dir = cubemap_face_negative_x;
		}
	} else {

		if(ay > ax && ay > az){
			m = ay;
			if(y > 0){
				//+y
				s =0.5f * (x / m + 1.0f);
				t = 0.5f * (z / m + 1.0f);
				major_dir = cubemap_face_positive_y;
			} else {
				s = 0.5f * (x / m + 1.0f);
				t = 0.5f * (-z / m + 1.0f);
				major_dir = cubemap_face_negative_y;
			}
		} else {
			m = az;
			if(z > 0){
				//+z
				s = 0.5f * (-x / m + 1.0f);
				t = 0.5f * (y / m + 1.0f);
				major_dir = cubemap_face_positive_z;
			} else {
				s = 0.5f * (x / m + 1.0f);
				t = 0.5f * (y / m + 1.0f);
				major_dir = cubemap_face_negative_z;
			}
		}
	}

	//��ʱ�Ȳ���ddx ddy
	if(ptex_->get_texture_type() != texture_type_cube)
	{
		EFLIB_ASSERT(false , "texture type not texture_type_cube.");
	}
	const texture_cube* pcube = static_cast<const texture_cube*>(ptex_);
	return sample_impl(&pcube->get_face(major_dir), s, t, 0, miplevel);
}

color_rgba32f sampler::sample_cube(
	const eflib::vec4& coord,
	const eflib::vec4& ddx,
	const eflib::vec4& ddy,
	float inv_x_w, float inv_y_w, float inv_w, float lod_bias
	) const
{
	float q = inv_w == 0 ? 1.0f : 1.0f / inv_w;
	vec4 origin_coord(coord * q);

	if(ptex_->get_texture_type() != texture_type_cube)
	{
		EFLIB_ASSERT(false , "texture type not texture_type_cube.");
	}
	const texture_cube* pcube = static_cast<const texture_cube*>(ptex_);
	float lod = calc_lod(origin_coord, int4(static_cast<int>(pcube->get_width(0)),
		static_cast<int>(pcube->get_height(0)), 0, 0), ddx, ddy, inv_x_w, inv_y_w, inv_w, lod_bias);
	//return color_rgba32f(vec4(coord.xyz(), 1.0f));
	//return color_rgba32f(invlod, invlod, invlod, 1.0f);
	return sample_cube(coord.x, coord.y, coord.z, lod);
}
END_NS_SALVIAR()