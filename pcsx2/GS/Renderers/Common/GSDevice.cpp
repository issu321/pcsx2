/*  PCSX2 - PS2 Emulator for PCs
 *  Copyright (C) 2002-2021 PCSX2 Dev Team
 *
 *  PCSX2 is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU Lesser General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  PCSX2 is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with PCSX2.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include "PrecompiledHeader.h"
#include "GSDevice.h"
#include "GS/GSGL.h"
#include "GS/GS.h"
#include "Host.h"
#include "common/StringUtil.h"

#include "imgui.h"

#include <algorithm>

const char* shaderName(ShaderConvert value)
{
	switch (value)
	{
			// clang-format off
		case ShaderConvert::COPY:                   return "ps_copy";
		case ShaderConvert::RGBA8_TO_16_BITS:       return "ps_convert_rgba8_16bits";
		case ShaderConvert::DATM_1:                 return "ps_datm1";
		case ShaderConvert::DATM_0:                 return "ps_datm0";
		case ShaderConvert::HDR_INIT:               return "ps_hdr_init";
		case ShaderConvert::HDR_RESOLVE:            return "ps_hdr_resolve";
		case ShaderConvert::TRANSPARENCY_FILTER:    return "ps_filter_transparency";
		case ShaderConvert::FLOAT32_TO_16_BITS:     return "ps_convert_float32_32bits";
		case ShaderConvert::FLOAT32_TO_32_BITS:     return "ps_convert_float32_32bits";
		case ShaderConvert::FLOAT32_TO_RGBA8:       return "ps_convert_float32_rgba8";
		case ShaderConvert::FLOAT16_TO_RGB5A1:      return "ps_convert_float16_rgb5a1";
		case ShaderConvert::RGBA8_TO_FLOAT32:       return "ps_convert_rgba8_float32";
		case ShaderConvert::RGBA8_TO_FLOAT24:       return "ps_convert_rgba8_float24";
		case ShaderConvert::RGBA8_TO_FLOAT16:       return "ps_convert_rgba8_float16";
		case ShaderConvert::RGB5A1_TO_FLOAT16:      return "ps_convert_rgb5a1_float16";
		case ShaderConvert::RGBA8_TO_FLOAT32_BILN:  return "ps_convert_rgba8_float32_biln";
		case ShaderConvert::RGBA8_TO_FLOAT24_BILN:  return "ps_convert_rgba8_float24_biln";
		case ShaderConvert::RGBA8_TO_FLOAT16_BILN:  return "ps_convert_rgba8_float16_biln";
		case ShaderConvert::RGB5A1_TO_FLOAT16_BILN: return "ps_convert_rgb5a1_float16_biln";
		case ShaderConvert::DEPTH_COPY:             return "ps_depth_copy";
		case ShaderConvert::RGBA_TO_8I:             return "ps_convert_rgba_8i";
		case ShaderConvert::CLUT_4:                 return "ps_convert_clut_4";
		case ShaderConvert::CLUT_8:                 return "ps_convert_clut_8";
		case ShaderConvert::YUV:                    return "ps_yuv";
			// clang-format on
		default:
			ASSERT(0);
			return "ShaderConvertUnknownShader";
	}
}

const char* shaderName(PresentShader value)
{
	switch (value)
	{
			// clang-format off
		case PresentShader::COPY:              return "ps_copy";
		case PresentShader::SCANLINE:          return "ps_filter_scanlines";
		case PresentShader::DIAGONAL_FILTER:   return "ps_filter_diagonal";
		case PresentShader::TRIANGULAR_FILTER: return "ps_filter_triangular";
		case PresentShader::COMPLEX_FILTER:    return "ps_filter_complex";
		case PresentShader::LOTTES_FILTER:     return "ps_filter_lottes";
			// clang-format on
		default:
			ASSERT(0);
			return "DisplayShaderUnknownShader";
	}
}

static int MipmapLevelsForSize(int width, int height)
{
	return std::min(static_cast<int>(std::log2(std::max(width, height))) + 1, MAXIMUM_TEXTURE_MIPMAP_LEVELS);
}

std::unique_ptr<GSDevice> g_gs_device;

GSDevice::GSDevice() = default;

GSDevice::~GSDevice()
{
	// should've been cleaned up in Destroy()
	pxAssert(m_pool.empty() && !m_merge && !m_weavebob && !m_blend && !m_mad && !m_target_tmp && !m_cas);
}

const char* GSDevice::RenderAPIToString(RenderAPI api)
{
	switch (api)
	{
		// clang-format off
#define CASE(x) case RenderAPI::x: return #x
		CASE(None);
		CASE(D3D11);
		CASE(D3D12);
		CASE(Metal);
		CASE(Vulkan);
		CASE(OpenGL);
		CASE(OpenGLES);
#undef CASE
		// clang-format on
	default:
		return "Unknown";
	}
}

bool GSDevice::ParseFullscreenMode(const std::string_view& mode, u32* width, u32* height, float* refresh_rate)
{
	if (!mode.empty())
	{
		std::string_view::size_type sep1 = mode.find('x');
		if (sep1 != std::string_view::npos)
		{
			std::optional<u32> owidth = StringUtil::FromChars<u32>(mode.substr(0, sep1));
			sep1++;

			while (sep1 < mode.length() && std::isspace(mode[sep1]))
				sep1++;

			if (owidth.has_value() && sep1 < mode.length())
			{
				std::string_view::size_type sep2 = mode.find('@', sep1);
				if (sep2 != std::string_view::npos)
				{
					std::optional<u32> oheight = StringUtil::FromChars<u32>(mode.substr(sep1, sep2 - sep1));
					sep2++;

					while (sep2 < mode.length() && std::isspace(mode[sep2]))
						sep2++;

					if (oheight.has_value() && sep2 < mode.length())
					{
						std::optional<float> orefresh_rate = StringUtil::FromChars<float>(mode.substr(sep2));
						if (orefresh_rate.has_value())
						{
							*width = owidth.value();
							*height = oheight.value();
							*refresh_rate = orefresh_rate.value();
							return true;
						}
					}
				}
			}
		}
	}

	*width = 0;
	*height = 0;
	*refresh_rate = 0;
	return false;
}

std::string GSDevice::GetFullscreenModeString(u32 width, u32 height, float refresh_rate)
{
	return StringUtil::StdStringFromFormat("%u x %u @ %f hz", width, height, refresh_rate);
}

bool GSDevice::Create(const WindowInfo& wi, VsyncMode vsync)
{
	m_window_info = wi;
	m_vsync_mode = vsync;
	return true;
}

void GSDevice::Destroy()
{
	if (m_imgui_font)
	{
		Recycle(m_imgui_font);
		m_imgui_font = nullptr;
	}

	ClearCurrent();
	PurgePool();
}

bool GSDevice::GetHostRefreshRate(float* refresh_rate)
{
	if (m_window_info.surface_refresh_rate > 0.0f)
	{
		*refresh_rate = m_window_info.surface_refresh_rate;
		return true;
	}

	return WindowInfo::QueryRefreshRateForWindow(m_window_info, refresh_rate);
}

bool GSDevice::UpdateImGuiFontTexture()
{
	unsigned char* pixels;
	int width, height;
	ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

	const GSVector4i r(0, 0, width, height);
	const int pitch = sizeof(u32) * width;

	if (m_imgui_font && m_imgui_font->GetWidth() == width && m_imgui_font->GetHeight() == height)
		return m_imgui_font->Update(r, pixels, pitch);

	GSTexture* new_font = CreateTexture(width, height, 1, GSTexture::Format::Color);
	if (!new_font || !new_font->Update(r, pixels, pitch))
		return false;

	if (m_imgui_font)
		Recycle(m_imgui_font);

	m_imgui_font = new_font;
	ImGui::GetIO().Fonts->SetTexID(new_font->GetNativeHandle());
	return true;
}

GSTexture* GSDevice::FetchSurface(GSTexture::Type type, int width, int height, int levels, GSTexture::Format format, bool clear, bool prefer_reuse)
{
	const GSVector2i size(width, height);
	const bool prefer_new_texture = (m_features.prefer_new_textures && type == GSTexture::Type::Texture && !prefer_reuse);

	GSTexture* t = nullptr;
	auto fallback = m_pool.end();

	for (auto i = m_pool.begin(); i != m_pool.end(); ++i)
	{
		t = *i;

		assert(t);

		if (t->GetType() == type && t->GetFormat() == format && t->GetSize() == size && t->GetMipmapLevels() == levels)
		{
			if (!prefer_new_texture || t->GetLastFrameUsed() != m_frame)
			{
				m_pool_memory_usage -= t->GetMemUsage();
				m_pool.erase(i);
				break;
			}
			else if (fallback == m_pool.end())
			{
				fallback = i;
			}
		}

		t = nullptr;
	}

	if (!t)
	{
		if (m_pool.size() >= MAX_POOLED_TEXTURES && fallback != m_pool.end())
		{
			t = *fallback;
			m_pool_memory_usage -= t->GetMemUsage();
			m_pool.erase(fallback);
		}
		else
		{
			t = CreateSurface(type, width, height, levels, format);
			if (!t)
				throw std::bad_alloc();
		}
	}

	switch (type)
	{
	case GSTexture::Type::RenderTarget:
		{
			if (clear)
				ClearRenderTarget(t, 0);
			else
				InvalidateRenderTarget(t);
		}
		break;
	case GSTexture::Type::DepthStencil:
		{
			if (clear)
				ClearDepth(t);
			else
				InvalidateRenderTarget(t);
		}
		break;
	default:
		break;
	}

	return t;
}

void GSDevice::Recycle(GSTexture* t)
{
	if (!t)
		return;

	t->SetLastFrameUsed(m_frame);

	m_pool.push_front(t);
	m_pool_memory_usage += t->GetMemUsage();

	//printf("%d\n",m_pool.size());

	while (m_pool.size() > MAX_POOLED_TEXTURES)
	{
		m_pool_memory_usage -= m_pool.back()->GetMemUsage();
		delete m_pool.back();

		m_pool.pop_back();
	}
}

bool GSDevice::UsesLowerLeftOrigin() const
{
	const RenderAPI api = GetRenderAPI();
	return (api == RenderAPI::OpenGL || api == RenderAPI::OpenGLES);
}

void GSDevice::AgePool()
{
	m_frame++;

	while (m_pool.size() > 40 && m_frame - m_pool.back()->GetLastFrameUsed() > 10)
	{
		m_pool_memory_usage -= m_pool.back()->GetMemUsage();
		delete m_pool.back();

		m_pool.pop_back();
	}
}

void GSDevice::PurgePool()
{
	for (auto t : m_pool)
		delete t;
	m_pool.clear();
	m_pool_memory_usage = 0;
}

GSTexture* GSDevice::CreateRenderTarget(int w, int h, GSTexture::Format format, bool clear)
{
	return FetchSurface(GSTexture::Type::RenderTarget, w, h, 1, format, clear, true);
}

GSTexture* GSDevice::CreateDepthStencil(int w, int h, GSTexture::Format format, bool clear)
{
	return FetchSurface(GSTexture::Type::DepthStencil, w, h, 1, format, clear, true);
}

GSTexture* GSDevice::CreateTexture(int w, int h, int mipmap_levels, GSTexture::Format format, bool prefer_reuse /* = false */)
{
	const int levels = mipmap_levels < 0 ? MipmapLevelsForSize(w, h) : mipmap_levels;
	return FetchSurface(GSTexture::Type::Texture, w, h, levels, format, false, prefer_reuse);
}

GSTexture::Format GSDevice::GetDefaultTextureFormat(GSTexture::Type type)
{
	if (type == GSTexture::Type::DepthStencil)
		return GSTexture::Format::DepthStencil;
	else
		return GSTexture::Format::Color;
}

void GSDevice::StretchRect(GSTexture* sTex, GSTexture* dTex, const GSVector4& dRect, ShaderConvert shader, bool linear)
{
	StretchRect(sTex, GSVector4(0, 0, 1, 1), dTex, dRect, shader, linear);
}

void GSDevice::DrawMultiStretchRects(
	const MultiStretchRect* rects, u32 num_rects, GSTexture* dTex, ShaderConvert shader)
{
	for (u32 i = 0; i < num_rects; i++)
	{
		const MultiStretchRect& sr = rects[i];
		pxAssert(shader == ShaderConvert::COPY || rects[0].wmask.wrgba == 0xf);
		if (rects[0].wmask.wrgba != 0xf)
		{
			g_gs_device->StretchRect(sr.src, sr.src_rect, dTex, sr.dst_rect, rects[0].wmask.wr,
				rects[0].wmask.wg, rects[0].wmask.wb, rects[0].wmask.wa);
		}
		else
		{
			g_gs_device->StretchRect(sr.src, sr.src_rect, dTex, sr.dst_rect, shader, sr.linear);
		}
	}
}

void GSDevice::SortMultiStretchRects(MultiStretchRect* rects, u32 num_rects)
{
	// Depending on num_rects, insertion sort may be better here.
	std::sort(rects, rects + num_rects, [](const MultiStretchRect& lhs, const MultiStretchRect& rhs) {
		return lhs.src < rhs.src || lhs.linear < rhs.linear;
	});
}

void GSDevice::ClearCurrent()
{
	m_current = nullptr;

	delete m_merge;
	delete m_weavebob;
	delete m_blend;
	delete m_mad;
	delete m_target_tmp;
	delete m_cas;

	m_merge = nullptr;
	m_weavebob = nullptr;
	m_blend = nullptr;
	m_mad = nullptr;
	m_target_tmp = nullptr;
	m_cas = nullptr;
}

void GSDevice::Merge(GSTexture* sTex[3], GSVector4* sRect, GSVector4* dRect, const GSVector2i& fs, const GSRegPMODE& PMODE, const GSRegEXTBUF& EXTBUF, const GSVector4& c)
{
	if (ResizeTarget(&m_merge, fs.x, fs.y))
		DoMerge(sTex, sRect, m_merge, dRect, PMODE, EXTBUF, c, GSConfig.PCRTCOffsets);

	m_current = m_merge;
}

void GSDevice::Interlace(const GSVector2i& ds, int field, int mode, float yoffset)
{
	static int bufIdx = 0;
	float offset = yoffset * static_cast<float>(field);
	offset = GSConfig.DisableInterlaceOffset ? 0.0f : offset;

	auto do_interlace = [this](GSTexture* sTex, GSTexture* dTex, ShaderInterlace shader, bool linear, float yoffset, int bufIdx) {
		const GSVector2i ds_i = dTex->GetSize();
		const GSVector2 ds = GSVector2(static_cast<float>(ds_i.x), static_cast<float>(ds_i.y));

		GSVector4 sRect = GSVector4(0.0f, 0.0f, 1.0f, 1.0f);
		GSVector4 dRect = GSVector4(0.0f, yoffset, ds.x, ds.y + yoffset);

		// Select the top or bottom half for MAD buffering.
		if (shader == ShaderInterlace::MAD_BUFFER)
		{
			const float half_size = ds.y * 0.5f;
			if ((bufIdx >> 1) == 1)
				dRect.y += half_size;
			else
				dRect.w -= half_size;
		}

		const InterlaceConstantBuffer cb = {
			GSVector4(static_cast<float>(bufIdx), 1.0f / ds.y, ds.y, MAD_SENSITIVITY)
		};

		GL_PUSH("DoInterlace %dx%d Shader:%d Linear:%d", ds_i.x, ds_i.y, static_cast<int>(shader), linear);
		DoInterlace(sTex, sRect, dTex, dRect, shader, linear, cb);
	};

	switch (mode)
	{
		case 0: // Weave
			ResizeTarget(&m_weavebob, ds.x, ds.y);
			do_interlace(m_merge, m_weavebob, ShaderInterlace::WEAVE, false, offset, field);
			m_current = m_weavebob;
			break;
		case 1: // Bob
			// Field is reversed here as we are countering the bounce.
			ResizeTarget(&m_weavebob, ds.x, ds.y);
			do_interlace(m_merge, m_weavebob, ShaderInterlace::BOB, true, yoffset * (1 - field), 0);
			m_current = m_weavebob;
			break;
		case 2: // Blend
			ResizeTarget(&m_weavebob, ds.x, ds.y);
			do_interlace(m_merge, m_weavebob, ShaderInterlace::WEAVE, false, offset, field);
			ResizeTarget(&m_blend, ds.x, ds.y);
			do_interlace(m_weavebob, m_blend, ShaderInterlace::BLEND, false, 0, 0);
			m_current = m_blend;
			break;
		case 3: // FastMAD Motion Adaptive Deinterlacing
			bufIdx++;
			bufIdx &= ~1;
			bufIdx |= field;
			bufIdx &= 3;
			ResizeTarget(&m_mad, ds.x, ds.y * 2.0f);
			do_interlace(m_merge, m_mad, ShaderInterlace::MAD_BUFFER, false, offset, bufIdx);
			ResizeTarget(&m_weavebob, ds.x, ds.y);
			do_interlace(m_mad, m_weavebob, ShaderInterlace::MAD_RECONSTRUCT, false, 0, bufIdx);
			m_current = m_weavebob;
			break;
		default:
			m_current = m_merge;
			break;
	}
}

void GSDevice::FXAA()
{
	// Combining FXAA+ShadeBoost can't share the same target.
	GSTexture*& dTex = (m_current == m_target_tmp) ? m_merge : m_target_tmp;
	if (ResizeTexture(&dTex, GSTexture::Type::RenderTarget, m_current->GetWidth(), m_current->GetHeight(), false, true))
	{
		InvalidateRenderTarget(dTex);
		DoFXAA(m_current, dTex);
		m_current = dTex;
	}
}

void GSDevice::ShadeBoost()
{
	if (ResizeTexture(&m_target_tmp, GSTexture::Type::RenderTarget, m_current->GetWidth(), m_current->GetHeight(), false, true))
	{
		InvalidateRenderTarget(m_target_tmp);

		// predivide to avoid the divide (multiply) in the shader
		const float params[4] = {
			static_cast<float>(GSConfig.ShadeBoost_Brightness) * (1.0f / 50.0f),
			static_cast<float>(GSConfig.ShadeBoost_Contrast) * (1.0f / 50.0f),
			static_cast<float>(GSConfig.ShadeBoost_Saturation) * (1.0f / 50.0f),
		};

		DoShadeBoost(m_current, m_target_tmp, params);

		m_current = m_target_tmp;
	}
}

void GSDevice::Resize(int width, int height)
{
	GSTexture*& dTex = (m_current == m_target_tmp) ? m_merge : m_target_tmp;
	GSVector2i s = m_current->GetSize();
	int multiplier = 1;

	while (width > s.x || height > s.y)
	{
		s = m_current->GetSize() * GSVector2i(++multiplier);
	}

	if (ResizeTexture(&dTex, GSTexture::Type::RenderTarget, s.x, s.y, false))
	{
		const GSVector4 sRect(0, 0, 1, 1);
		const GSVector4 dRect(0, 0, s.x, s.y);
		StretchRect(m_current, sRect, dTex, dRect, ShaderConvert::COPY, false);
		m_current = dTex;
	}
}

bool GSDevice::ResizeTexture(GSTexture** t, GSTexture::Type type, int w, int h, bool clear, bool prefer_reuse)
{
	if (t == NULL)
	{
		ASSERT(0);
		return false;
	}

	GSTexture* t2 = *t;

	if (t2 == NULL || t2->GetWidth() != w || t2->GetHeight() != h)
	{
		const GSTexture::Format fmt = t2 ? t2->GetFormat() : GetDefaultTextureFormat(type);
		const int levels = t2 ? (t2->IsMipmap() ? MipmapLevelsForSize(w, h) : 1) : 1;

		GSTexture* new_t = FetchSurface(type, w, h, levels, fmt, clear, prefer_reuse);
		if (new_t)
		{
			if (t2)
			{
				// TODO: We probably want to make this optional if we're overwriting it...
				const GSVector4 sRect(0, 0, 1, 1);
				const GSVector4 dRect(0, 0, t2->GetWidth(), t2->GetHeight());
				StretchRect(t2, sRect, new_t, dRect, ShaderConvert::COPY, true);
				Recycle(t2);
			}

			t2 = new_t;
			*t = t2;
		}
	}

	return t2 != NULL;
}

bool GSDevice::ResizeTexture(GSTexture** t, int w, int h, bool prefer_reuse)
{
	return ResizeTexture(t, GSTexture::Type::Texture, w, h, false, prefer_reuse);
}

bool GSDevice::ResizeTarget(GSTexture** t, int w, int h)
{
	return ResizeTexture(t, GSTexture::Type::RenderTarget, w, h);
}

bool GSDevice::ResizeTarget(GSTexture** t)
{
	GSVector2i s = m_current->GetSize();
	return ResizeTexture(t, GSTexture::Type::RenderTarget, s.x, s.y);
}

void GSDevice::SetHWDrawConfigForAlphaPass(GSHWDrawConfig::PSSelector* ps,
	GSHWDrawConfig::ColorMaskSelector* cms,
	GSHWDrawConfig::BlendState* bs,
	GSHWDrawConfig::DepthStencilSelector* dss)
{
	// only need to compute the alpha component (allow the shader to optimize better)
	ps->no_ablend = false;
	ps->only_alpha = true;

	// definitely don't need to compute software blend (this may get rid of some barriers)
	ps->blend_a = ps->blend_b = ps->blend_c = ps->blend_d = 0;

	// only write alpha (RGB=0,A=1)
	cms->wrgba = (1 << 3);

	// no need for hardware blending, since we're not writing RGB
	bs->enable = false;

	// if depth writes are on, we can optimize to an EQUAL test, otherwise we leave the tests alone
	// since the alpha channel isn't blended, the last fragment wins and this'll be okay
	if (dss->zwe)
	{
		dss->zwe = false;
		dss->ztst = ZTST_GEQUAL;
	}
}

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wignored-qualifiers"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-qualifiers"
#endif

// Kinda grotty, but better than copy/pasting the relevant bits in..
#define A_CPU 1
#include "bin/resources/shaders/common/ffx_a.h"
#include "bin/resources/shaders/common/ffx_cas.h"

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

bool GSDevice::GetCASShaderSource(std::string* source)
{
	std::optional<std::string> ffx_a_source(Host::ReadResourceFileToString("shaders/common/ffx_a.h"));
	std::optional<std::string> ffx_cas_source(Host::ReadResourceFileToString("shaders/common/ffx_cas.h"));
	if (!ffx_a_source.has_value() || !ffx_cas_source.has_value())
		return false;

	// Since our shader compilers don't support includes, and OpenGL doesn't at all... we'll do a really cheeky string replace.
	StringUtil::ReplaceAll(source, "#include \"ffx_a.h\"", ffx_a_source.value());
	StringUtil::ReplaceAll(source, "#include \"ffx_cas.h\"", ffx_cas_source.value());
	return true;
}

void GSDevice::CAS(GSTexture*& tex, GSVector4i& src_rect, GSVector4& src_uv, const GSVector4& draw_rect, bool sharpen_only)
{
	const int dst_width = sharpen_only ? src_rect.width() : static_cast<int>(std::ceil(draw_rect.z - draw_rect.x));
	const int dst_height = sharpen_only ? src_rect.height() : static_cast<int>(std::ceil(draw_rect.w - draw_rect.y));
	const int src_offset_x = static_cast<int>(src_rect.x);
	const int src_offset_y = static_cast<int>(src_rect.y);

	GSTexture* src_tex = tex;
	if (!m_cas || m_cas->GetWidth() != dst_width || m_cas->GetHeight() != dst_height)
	{
		delete m_cas;
		m_cas = CreateSurface(GSTexture::Type::RWTexture, dst_width, dst_height, 1, GSTexture::Format::Color);
		if (!m_cas)
		{
			Console.Error("Failed to allocate CAS RW texture.");
			return;
		}
	}

	std::array<u32, NUM_CAS_CONSTANTS> consts;
	CasSetup(&consts[0], &consts[4], static_cast<float>(GSConfig.CAS_Sharpness) * 0.01f,
		static_cast<AF1>(src_rect.width()), static_cast<AF1>(src_rect.height()),
		static_cast<AF1>(dst_width), static_cast<AF1>(dst_height));
	consts[8] = static_cast<u32>(src_offset_x);
	consts[9] = static_cast<u32>(src_offset_y);

	if (!DoCAS(src_tex, m_cas, sharpen_only, consts))
	{
		// leave textures intact if we failed
		Console.Warning("Applying CAS failed.");
		return;
	}

	tex = m_cas;
	src_rect = GSVector4i(0, 0, dst_width, dst_height);
	src_uv = GSVector4(0.0f, 0.0f, 1.0f, 1.0f);
}

// clang-format off

const std::array<u8, 16> GSDevice::m_replaceDualSrcBlendMap =
{{
	SRC_COLOR,        // SRC_COLOR
	INV_SRC_COLOR,    // INV_SRC_COLOR
	DST_COLOR,        // DST_COLOR
	INV_DST_COLOR,    // INV_DST_COLOR
	SRC_COLOR,        // SRC1_COLOR
	INV_SRC_COLOR,    // INV_SRC1_COLOR
	SRC_ALPHA,        // SRC_ALPHA
	INV_SRC_ALPHA,    // INV_SRC_ALPHA
	DST_ALPHA,        // DST_ALPHA
	INV_DST_ALPHA,    // INV_DST_ALPHA
	SRC_ALPHA,        // SRC1_ALPHA
	INV_SRC_ALPHA,    // INV_SRC1_ALPHA
	CONST_COLOR,      // CONST_COLOR
	INV_CONST_COLOR,  // INV_CONST_COLOR
	CONST_ONE,        // CONST_ONE
	CONST_ZERO        // CONST_ZERO
}};

const std::array<HWBlend, 3*3*3*3> GSDevice::m_blendMap =
{{
	{ BLEND_NO_REC             , OP_ADD          , CONST_ONE       , CONST_ZERO}      , // 0000: (Cs - Cs)*As + Cs ==> Cs
	{ BLEND_CD                 , OP_ADD          , CONST_ZERO      , CONST_ONE}       , // 0001: (Cs - Cs)*As + Cd ==> Cd
	{ BLEND_NO_REC             , OP_ADD          , CONST_ZERO      , CONST_ZERO}      , // 0002: (Cs - Cs)*As +  0 ==> 0
	{ BLEND_NO_REC             , OP_ADD          , CONST_ONE       , CONST_ZERO}      , // 0010: (Cs - Cs)*Ad + Cs ==> Cs
	{ BLEND_CD                 , OP_ADD          , CONST_ZERO      , CONST_ONE}       , // 0011: (Cs - Cs)*Ad + Cd ==> Cd
	{ BLEND_NO_REC             , OP_ADD          , CONST_ZERO      , CONST_ZERO}      , // 0012: (Cs - Cs)*Ad +  0 ==> 0
	{ BLEND_NO_REC             , OP_ADD          , CONST_ONE       , CONST_ZERO}      , // 0020: (Cs - Cs)*F  + Cs ==> Cs
	{ BLEND_CD                 , OP_ADD          , CONST_ZERO      , CONST_ONE}       , // 0021: (Cs - Cs)*F  + Cd ==> Cd
	{ BLEND_NO_REC             , OP_ADD          , CONST_ZERO      , CONST_ZERO}      , // 0022: (Cs - Cs)*F  +  0 ==> 0
	{ BLEND_A_MAX | BLEND_MIX2 , OP_SUBTRACT     , CONST_ONE       , SRC1_ALPHA}      , // 0100: (Cs - Cd)*As + Cs ==> Cs*(As + 1) - Cd*As
	{ BLEND_MIX1               , OP_ADD          , SRC1_ALPHA      , INV_SRC1_ALPHA}  , // 0101: (Cs - Cd)*As + Cd ==> Cs*As + Cd*(1 - As)
	{ BLEND_MIX1               , OP_SUBTRACT     , SRC1_ALPHA      , SRC1_ALPHA}      , // 0102: (Cs - Cd)*As +  0 ==> Cs*As - Cd*As
	{ BLEND_A_MAX              , OP_SUBTRACT     , CONST_ONE       , DST_ALPHA}       , // 0110: (Cs - Cd)*Ad + Cs ==> Cs*(Ad + 1) - Cd*Ad
	{ 0                        , OP_ADD          , DST_ALPHA       , INV_DST_ALPHA}   , // 0111: (Cs - Cd)*Ad + Cd ==> Cs*Ad + Cd*(1 - Ad)
	{ 0                        , OP_SUBTRACT     , DST_ALPHA       , DST_ALPHA}       , // 0112: (Cs - Cd)*Ad +  0 ==> Cs*Ad - Cd*Ad
	{ BLEND_A_MAX | BLEND_MIX2 , OP_SUBTRACT     , CONST_ONE       , CONST_COLOR}     , // 0120: (Cs - Cd)*F  + Cs ==> Cs*(F + 1) - Cd*F
	{ BLEND_MIX1               , OP_ADD          , CONST_COLOR     , INV_CONST_COLOR} , // 0121: (Cs - Cd)*F  + Cd ==> Cs*F + Cd*(1 - F)
	{ BLEND_MIX1               , OP_SUBTRACT     , CONST_COLOR     , CONST_COLOR}     , // 0122: (Cs - Cd)*F  +  0 ==> Cs*F - Cd*F
	{ BLEND_NO_REC             , OP_ADD          , CONST_ONE       , CONST_ZERO}      , // 0200: (Cs -  0)*As + Cs ==> Cs*(As + 1)
	{ BLEND_ACCU               , OP_ADD          , SRC1_ALPHA      , CONST_ONE}       , // 0201: (Cs -  0)*As + Cd ==> Cs*As + Cd
	{ BLEND_NO_REC             , OP_ADD          , SRC1_ALPHA      , CONST_ZERO}      , // 0202: (Cs -  0)*As +  0 ==> Cs*As
	{ BLEND_A_MAX              , OP_ADD          , CONST_ONE       , CONST_ZERO}      , // 0210: (Cs -  0)*Ad + Cs ==> Cs*(Ad + 1)
	{ BLEND_HW_CLR3            , OP_ADD          , DST_ALPHA       , CONST_ONE}       , // 0211: (Cs -  0)*Ad + Cd ==> Cs*Ad + Cd
	{ BLEND_HW_CLR3            , OP_ADD          , DST_ALPHA       , CONST_ZERO}      , // 0212: (Cs -  0)*Ad +  0 ==> Cs*Ad
	{ BLEND_NO_REC             , OP_ADD          , CONST_ONE       , CONST_ZERO}      , // 0220: (Cs -  0)*F  + Cs ==> Cs*(F + 1)
	{ BLEND_ACCU               , OP_ADD          , CONST_COLOR     , CONST_ONE}       , // 0221: (Cs -  0)*F  + Cd ==> Cs*F + Cd
	{ BLEND_NO_REC             , OP_ADD          , CONST_COLOR     , CONST_ZERO}      , // 0222: (Cs -  0)*F  +  0 ==> Cs*F
	{ BLEND_MIX3               , OP_ADD          , INV_SRC1_ALPHA  , SRC1_ALPHA}      , // 1000: (Cd - Cs)*As + Cs ==> Cd*As + Cs*(1 - As)
	{ BLEND_A_MAX | BLEND_MIX1 , OP_REV_SUBTRACT , SRC1_ALPHA      , CONST_ONE}       , // 1001: (Cd - Cs)*As + Cd ==> Cd*(As + 1) - Cs*As
	{ BLEND_MIX1               , OP_REV_SUBTRACT , SRC1_ALPHA      , SRC1_ALPHA}      , // 1002: (Cd - Cs)*As +  0 ==> Cd*As - Cs*As
	{ 0                        , OP_ADD          , INV_DST_ALPHA   , DST_ALPHA}       , // 1010: (Cd - Cs)*Ad + Cs ==> Cd*Ad + Cs*(1 - Ad)
	{ BLEND_A_MAX              , OP_REV_SUBTRACT , DST_ALPHA       , CONST_ONE}       , // 1011: (Cd - Cs)*Ad + Cd ==> Cd*(Ad + 1) - Cs*Ad
	{ 0                        , OP_REV_SUBTRACT , DST_ALPHA       , DST_ALPHA}       , // 1012: (Cd - Cs)*Ad +  0 ==> Cd*Ad - Cs*Ad
	{ BLEND_MIX3               , OP_ADD          , INV_CONST_COLOR , CONST_COLOR}     , // 1020: (Cd - Cs)*F  + Cs ==> Cd*F + Cs*(1 - F)
	{ BLEND_A_MAX | BLEND_MIX1 , OP_REV_SUBTRACT , CONST_COLOR     , CONST_ONE}       , // 1021: (Cd - Cs)*F  + Cd ==> Cd*(F + 1) - Cs*F
	{ BLEND_MIX1               , OP_REV_SUBTRACT , CONST_COLOR     , CONST_COLOR}     , // 1022: (Cd - Cs)*F  +  0 ==> Cd*F - Cs*F
	{ BLEND_NO_REC             , OP_ADD          , CONST_ONE       , CONST_ZERO}      , // 1100: (Cd - Cd)*As + Cs ==> Cs
	{ BLEND_CD                 , OP_ADD          , CONST_ZERO      , CONST_ONE}       , // 1101: (Cd - Cd)*As + Cd ==> Cd
	{ BLEND_NO_REC             , OP_ADD          , CONST_ZERO      , CONST_ZERO}      , // 1102: (Cd - Cd)*As +  0 ==> 0
	{ BLEND_NO_REC             , OP_ADD          , CONST_ONE       , CONST_ZERO}      , // 1110: (Cd - Cd)*Ad + Cs ==> Cs
	{ BLEND_CD                 , OP_ADD          , CONST_ZERO      , CONST_ONE}       , // 1111: (Cd - Cd)*Ad + Cd ==> Cd
	{ BLEND_NO_REC             , OP_ADD          , CONST_ZERO      , CONST_ZERO}      , // 1112: (Cd - Cd)*Ad +  0 ==> 0
	{ BLEND_NO_REC             , OP_ADD          , CONST_ONE       , CONST_ZERO}      , // 1120: (Cd - Cd)*F  + Cs ==> Cs
	{ BLEND_CD                 , OP_ADD          , CONST_ZERO      , CONST_ONE}       , // 1121: (Cd - Cd)*F  + Cd ==> Cd
	{ BLEND_NO_REC             , OP_ADD          , CONST_ZERO      , CONST_ZERO}      , // 1122: (Cd - Cd)*F  +  0 ==> 0
	{ 0                        , OP_ADD          , CONST_ONE       , SRC1_ALPHA}      , // 1200: (Cd -  0)*As + Cs ==> Cs + Cd*As
	{ BLEND_HW_CLR1            , OP_ADD          , DST_COLOR       , SRC1_ALPHA}      , // 1201: (Cd -  0)*As + Cd ==> Cd*(1 + As)
	{ BLEND_HW_CLR2            , OP_ADD          , DST_COLOR       , SRC1_ALPHA}      , // 1202: (Cd -  0)*As +  0 ==> Cd*As
	{ 0                        , OP_ADD          , CONST_ONE       , DST_ALPHA}       , // 1210: (Cd -  0)*Ad + Cs ==> Cs + Cd*Ad
	{ BLEND_HW_CLR1            , OP_ADD          , DST_COLOR       , DST_ALPHA}       , // 1211: (Cd -  0)*Ad + Cd ==> Cd*(1 + Ad)
	{ 0                        , OP_ADD          , CONST_ZERO      , DST_ALPHA}       , // 1212: (Cd -  0)*Ad +  0 ==> Cd*Ad
	{ 0                        , OP_ADD          , CONST_ONE       , CONST_COLOR}     , // 1220: (Cd -  0)*F  + Cs ==> Cs + Cd*F
	{ BLEND_HW_CLR1            , OP_ADD          , DST_COLOR       , CONST_COLOR}     , // 1221: (Cd -  0)*F  + Cd ==> Cd*(1 + F)
	{ BLEND_HW_CLR2            , OP_ADD          , DST_COLOR       , CONST_COLOR}     , // 1222: (Cd -  0)*F  +  0 ==> Cd*F
	{ BLEND_NO_REC             , OP_ADD          , INV_SRC1_ALPHA  , CONST_ZERO}      , // 2000: (0  - Cs)*As + Cs ==> Cs*(1 - As)
	{ BLEND_ACCU               , OP_REV_SUBTRACT , SRC1_ALPHA      , CONST_ONE}       , // 2001: (0  - Cs)*As + Cd ==> Cd - Cs*As
	{ BLEND_NO_REC             , OP_REV_SUBTRACT , SRC1_ALPHA      , CONST_ZERO}      , // 2002: (0  - Cs)*As +  0 ==> 0 - Cs*As
	{ 0                        , OP_ADD          , INV_DST_ALPHA   , CONST_ZERO}      , // 2010: (0  - Cs)*Ad + Cs ==> Cs*(1 - Ad)
	{ BLEND_HW_CLR3            , OP_REV_SUBTRACT , DST_ALPHA       , CONST_ONE}       , // 2011: (0  - Cs)*Ad + Cd ==> Cd - Cs*Ad
	{ 0                        , OP_REV_SUBTRACT , DST_ALPHA       , CONST_ZERO}      , // 2012: (0  - Cs)*Ad +  0 ==> 0 - Cs*Ad
	{ BLEND_NO_REC             , OP_ADD          , INV_CONST_COLOR , CONST_ZERO}      , // 2020: (0  - Cs)*F  + Cs ==> Cs*(1 - F)
	{ BLEND_ACCU               , OP_REV_SUBTRACT , CONST_COLOR     , CONST_ONE}       , // 2021: (0  - Cs)*F  + Cd ==> Cd - Cs*F
	{ BLEND_NO_REC             , OP_REV_SUBTRACT , CONST_COLOR     , CONST_ZERO}      , // 2022: (0  - Cs)*F  +  0 ==> 0 - Cs*F
	{ 0                        , OP_SUBTRACT     , CONST_ONE       , SRC1_ALPHA}      , // 2100: (0  - Cd)*As + Cs ==> Cs - Cd*As
	{ 0                        , OP_ADD          , CONST_ZERO      , INV_SRC1_ALPHA}  , // 2101: (0  - Cd)*As + Cd ==> Cd*(1 - As)
	{ 0                        , OP_SUBTRACT     , CONST_ZERO      , SRC1_ALPHA}      , // 2102: (0  - Cd)*As +  0 ==> 0 - Cd*As
	{ 0                        , OP_SUBTRACT     , CONST_ONE       , DST_ALPHA}       , // 2110: (0  - Cd)*Ad + Cs ==> Cs - Cd*Ad
	{ 0                        , OP_ADD          , CONST_ZERO      , INV_DST_ALPHA}   , // 2111: (0  - Cd)*Ad + Cd ==> Cd*(1 - Ad)
	{ 0                        , OP_SUBTRACT     , CONST_ONE       , DST_ALPHA}       , // 2112: (0  - Cd)*Ad +  0 ==> 0 - Cd*Ad
	{ 0                        , OP_SUBTRACT     , CONST_ONE       , CONST_COLOR}     , // 2120: (0  - Cd)*F  + Cs ==> Cs - Cd*F
	{ 0                        , OP_ADD          , CONST_ZERO      , INV_CONST_COLOR} , // 2121: (0  - Cd)*F  + Cd ==> Cd*(1 - F)
	{ 0                        , OP_SUBTRACT     , CONST_ONE       , CONST_COLOR}     , // 2122: (0  - Cd)*F  +  0 ==> 0 - Cd*F
	{ BLEND_NO_REC             , OP_ADD          , CONST_ONE       , CONST_ZERO}      , // 2200: (0  -  0)*As + Cs ==> Cs
	{ BLEND_CD                 , OP_ADD          , CONST_ZERO      , CONST_ONE}       , // 2201: (0  -  0)*As + Cd ==> Cd
	{ BLEND_NO_REC             , OP_ADD          , CONST_ZERO      , CONST_ZERO}      , // 2202: (0  -  0)*As +  0 ==> 0
	{ BLEND_NO_REC             , OP_ADD          , CONST_ONE       , CONST_ZERO}      , // 2210: (0  -  0)*Ad + Cs ==> Cs
	{ BLEND_CD                 , OP_ADD          , CONST_ZERO      , CONST_ONE}       , // 2211: (0  -  0)*Ad + Cd ==> Cd
	{ BLEND_NO_REC             , OP_ADD          , CONST_ZERO      , CONST_ZERO}      , // 2212: (0  -  0)*Ad +  0 ==> 0
	{ BLEND_NO_REC             , OP_ADD          , CONST_ONE       , CONST_ZERO}      , // 2220: (0  -  0)*F  + Cs ==> Cs
	{ BLEND_CD                 , OP_ADD          , CONST_ZERO      , CONST_ONE}       , // 2221: (0  -  0)*F  + Cd ==> Cd
	{ BLEND_NO_REC             , OP_ADD          , CONST_ZERO      , CONST_ZERO}      , // 2222: (0  -  0)*F  +  0 ==> 0
}};
