/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "rmlrendersoft.h"

#include "dbgprint.h"
#include "softscreen.h"
#include "ui/rml/rmlrendermath.h"
#include "ui/rml/rmltexture.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

static int const UI_MAX_TEXTURE_DIMENSION = 4096;

struct SoftGeometry
{
	std::vector<Rml::Vertex> Vertices;
	std::vector<int> Indices;
};


static UIRenderMaskOperation Mask_Operation(Rml::ClipMaskOperation operation)
{
	switch (operation) {
		case Rml::ClipMaskOperation::SetInverse:
			return(UI_RENDER_MASK_SET_INVERSE);
		case Rml::ClipMaskOperation::Intersect:
			return(UI_RENDER_MASK_INTERSECT);
		default:
			return(UI_RENDER_MASK_SET);
	}
}


void UIRmlSoftRenderClass::Report(char const * message)
{
	DebugString("UI: %s\n", message);
}


bool UIRmlSoftRenderClass::Init(void)
{
	White.Width = 1;
	White.Height = 1;
	White.Pixels.assign(1, 0xFFFFFFFFu);
	IsReady = true;
	return(true);
}


void UIRmlSoftRenderClass::Shutdown(void)
{
	IsReady = false;
	Stencil.clear();
	StencilWidth = 0;
	StencilHeight = 0;
}


void UIRmlSoftRenderClass::Ensure_Stencil(void)
{
	SoftScreen const & screen = Soft_Screen();
	if (StencilWidth != screen.Width || StencilHeight != screen.Height) {
		StencilWidth = screen.Width;
		StencilHeight = screen.Height;
		Stencil.assign((size_t)StencilWidth * (size_t)StencilHeight, 0);
	}
}


void UIRmlSoftRenderClass::Begin_Frame(int x, int y, int width, int height)
{
	Statistics.DrawCalls = 0;
	TransformEnabled = false;
	ClipMaskEnabled = false;
	StencilReference = 0;
	ViewX = x;
	ViewY = y;
	ViewWidth = width;
	ViewHeight = height;
	Ensure_Stencil();
}


void UIRmlSoftRenderClass::Begin_Dev_Frame(int x, int y, int width, int height)
{
	ViewX = x;
	ViewY = y;
	ViewWidth = width;
	ViewHeight = height;
}


void UIRmlSoftRenderClass::Render_ImGui(ImDrawData *)
{
	// The developer overlay is not drawn by the software renderer yet.
}


void UIRmlSoftRenderClass::Destroy_ImGui_Textures(void)
{
}


int UIRmlSoftRenderClass::Texture_Limit(void) const
{
	return(IsReady ? UI_MAX_TEXTURE_DIMENSION : 0);
}


void UIRmlSoftRenderClass::Log_Resource_Counts(char const * when) const
{
	DebugString("UI: %s; %u geometries (%llu bytes), %u textures (%llu bytes)%s%s\n",
				when, Statistics.GeometryCount, (unsigned long long)Statistics.GeometryBytes,
				Statistics.TextureCount, (unsigned long long)Statistics.TextureBytes,
				Error()[0] != '\0' ? "; error: " : "", Error());
}


Rml::CompiledGeometryHandle UIRmlSoftRenderClass::CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices)
{
	if (vertices.empty() || indices.empty() || indices.size() % 3 != 0) {
		return(0);
	}
	for (int index : indices) {
		if (index < 0 || (size_t)index >= vertices.size()) {
			Fail("a fragment's index is outside its vertices");
			return(0);
		}
	}

	SoftGeometry * geometry = new SoftGeometry;
	geometry->Vertices.assign(vertices.begin(), vertices.end());
	geometry->Indices.assign(indices.begin(), indices.end());
	Statistics.GeometryCount++;
	Statistics.GeometryBytes += vertices.size() * sizeof(Rml::Vertex) + indices.size() * sizeof(int);
	return((Rml::CompiledGeometryHandle)geometry);
}


void UIRmlSoftRenderClass::ReleaseGeometry(Rml::CompiledGeometryHandle handle)
{
	if (handle == 0) {
		return;
	}
	SoftGeometry * geometry = (SoftGeometry *)handle;
	Statistics.GeometryCount--;
	Statistics.GeometryBytes -= geometry->Vertices.size() * sizeof(Rml::Vertex) + geometry->Indices.size() * sizeof(int);
	delete geometry;
}


void UIRmlSoftRenderClass::Transform_Vertices(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation, std::vector<ScreenVertex> & out) const
{
	SoftGeometry const * geometry = (SoftGeometry const *)handle;

	float model[16];
	UI_Render_Model_Matrix(TransformEnabled ? Transform : nullptr, translation.x, translation.y, model);

	out.resize(geometry->Vertices.size());
	for (size_t index = 0; index < geometry->Vertices.size(); index++) {
		Rml::Vertex const & in = geometry->Vertices[index];
		float x = model[0] * in.position.x + model[4] * in.position.y + model[12];
		float y = model[1] * in.position.x + model[5] * in.position.y + model[13];
		float w = model[3] * in.position.x + model[7] * in.position.y + model[15];
		if (w != 1.0f && w > 0.0f) {
			x /= w;
			y /= w;
		}
		ScreenVertex & v = out[index];
		v.X = x + (float)ViewX;
		v.Y = y + (float)ViewY;
		v.U = in.tex_coord.x;
		v.V = in.tex_coord.y;
		v.R = in.colour.red;
		v.G = in.colour.green;
		v.B = in.colour.blue;
		v.A = in.colour.alpha;
	}
}


// The pixels drawing may touch: the view, narrowed by the scissor when one is set.
bool UIRmlSoftRenderClass::Clip_Rect(int & x0, int & y0, int & x1, int & y1) const
{
	SoftScreen const & screen = Soft_Screen();
	x0 = std::max(ViewX, 0);
	y0 = std::max(ViewY, 0);
	x1 = std::min(ViewX + ViewWidth, screen.Width);
	y1 = std::min(ViewY + ViewHeight, screen.Height);

	if (ScissorEnabled) {
		UIRenderClip clip;
		if (!Scissor.Valid() || !UI_Render_Clip_Rect((float)Scissor.Left(), (float)Scissor.Top(), (float)Scissor.Right(), (float)Scissor.Bottom(), ViewX, ViewY, ViewWidth, ViewHeight, clip)) {
			return(false);
		}
		x0 = std::max(x0, (int)clip.X);
		y0 = std::max(y0, (int)clip.Y);
		x1 = std::min(x1, (int)clip.X + (int)clip.Width);
		y1 = std::min(y1, (int)clip.Y + (int)clip.Height);
	}
	return(x1 > x0 && y1 > y0);
}


static inline bool Top_Left(float ax, float ay, float bx, float by)
{
	// With the triangle wound clockwise on screen (y down), a top edge runs exactly left to
	// right and a left edge runs upwards.
	return((ay == by && bx > ax) || (by < ay));
}


void UIRmlSoftRenderClass::Draw_Triangles(std::vector<ScreenVertex> const & vertices, std::vector<int> const & indices, Texture const * texture, DrawKind kind, std::uint8_t value)
{
	SoftScreen const & screen = Soft_Screen();
	if (screen.Pixels == nullptr) {
		return;
	}

	int clipx0, clipy0, clipx1, clipy1;
	if (!Clip_Rect(clipx0, clipy0, clipx1, clipy1)) {
		return;
	}

	bool const test_mask = (kind == DRAW_COLOUR) && ClipMaskEnabled;
	if (kind != DRAW_COLOUR || test_mask) {
		Ensure_Stencil();
	}

	for (size_t t = 0; t + 2 < indices.size(); t += 3) {
		ScreenVertex const * a = &vertices[indices[t]];
		ScreenVertex const * b = &vertices[indices[t + 1]];
		ScreenVertex const * c = &vertices[indices[t + 2]];

		float area = (b->X - a->X) * (c->Y - a->Y) - (c->X - a->X) * (b->Y - a->Y);
		if (area == 0.0f || !std::isfinite(area)) {
			continue;
		}
		if (area < 0.0f) {
			std::swap(b, c);
			area = -area;
		}

		int x0 = std::max(clipx0, (int)std::floor(std::min({a->X, b->X, c->X})));
		int y0 = std::max(clipy0, (int)std::floor(std::min({a->Y, b->Y, c->Y})));
		int x1 = std::min(clipx1, (int)std::ceil(std::max({a->X, b->X, c->X})));
		int y1 = std::min(clipy1, (int)std::ceil(std::max({a->Y, b->Y, c->Y})));
		if (x1 <= x0 || y1 <= y0) {
			continue;
		}

		float const inv = 1.0f / area;
		bool const tl0 = Top_Left(b->X, b->Y, c->X, c->Y);
		bool const tl1 = Top_Left(c->X, c->Y, a->X, a->Y);
		bool const tl2 = Top_Left(a->X, a->Y, b->X, b->Y);

		// Edge function steps per pixel; w0 weights a, w1 weights b, w2 weights c.
		float const e0dx = -(c->Y - b->Y), e0dy = (c->X - b->X);
		float const e1dx = -(a->Y - c->Y), e1dy = (a->X - c->X);
		float const e2dx = -(b->Y - a->Y), e2dy = (b->X - a->X);

		bool const uniform_colour = a->R == b->R && a->R == c->R && a->G == b->G && a->G == c->G
			&& a->B == b->B && a->B == c->B && a->A == b->A && a->A == c->A;

		for (int y = y0; y < y1; y++) {
			float const py = (float)y + 0.5f;
			float const px0 = (float)x0 + 0.5f;
			float w0 = (px0 - b->X) * e0dx + (py - b->Y) * e0dy;
			float w1 = (px0 - c->X) * e1dx + (py - c->Y) * e1dy;
			float w2 = (px0 - a->X) * e2dx + (py - a->Y) * e2dy;
			std::uint32_t * row = screen.Pixels + (size_t)y * (size_t)screen.Pitch;
			std::uint8_t * mask = test_mask || kind != DRAW_COLOUR ? &Stencil[(size_t)y * (size_t)StencilWidth] : nullptr;

			for (int x = x0; x < x1; x++, w0 += e0dx, w1 += e1dx, w2 += e2dx) {
				if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;
				if ((w0 == 0.0f && !tl0) || (w1 == 0.0f && !tl1) || (w2 == 0.0f && !tl2)) continue;

				if (kind == DRAW_MASK_WRITE) {
					mask[x] = value;
					continue;
				}
				if (kind == DRAW_MASK_INCREMENT) {
					if (mask[x] < 255) mask[x]++;
					continue;
				}
				if (test_mask && mask[x] != StencilReference) {
					continue;
				}

				float const l0 = w0 * inv, l1 = w1 * inv, l2 = w2 * inv;

				unsigned r, g, bl, al;
				if (uniform_colour) {
					r = a->R; g = a->G; bl = a->B; al = a->A;
				} else {
					r = (unsigned)(l0 * a->R + l1 * b->R + l2 * c->R + 0.5f);
					g = (unsigned)(l0 * a->G + l1 * b->G + l2 * c->G + 0.5f);
					bl = (unsigned)(l0 * a->B + l1 * b->B + l2 * c->B + 0.5f);
					al = (unsigned)(l0 * a->A + l1 * b->A + l2 * c->A + 0.5f);
				}

				if (texture != nullptr) {
					float u = l0 * a->U + l1 * b->U + l2 * c->U;
					float v = l0 * a->V + l1 * b->V + l2 * c->V;
					int tx = (int)(u * (float)texture->Width);
					int ty = (int)(v * (float)texture->Height);
					tx = tx < 0 ? 0 : (tx >= texture->Width ? texture->Width - 1 : tx);
					ty = ty < 0 ? 0 : (ty >= texture->Height ? texture->Height - 1 : ty);
					std::uint32_t const texel = texture->Pixels[(size_t)ty * (size_t)texture->Width + (size_t)tx];
					r = (r * (texel & 0xFF) + 127) / 255;
					g = (g * ((texel >> 8) & 0xFF) + 127) / 255;
					bl = (bl * ((texel >> 16) & 0xFF) + 127) / 255;
					al = (al * (texel >> 24) + 127) / 255;
				}

				if (al == 0 && r == 0 && g == 0 && bl == 0) {
					continue;
				}

				std::uint32_t const dest = row[x];
				unsigned const keep = 255 - al;
				unsigned const dr = r + (((dest >> 16) & 0xFF) * keep + 127) / 255;
				unsigned const dg = g + (((dest >> 8) & 0xFF) * keep + 127) / 255;
				unsigned const db = bl + ((dest & 0xFF) * keep + 127) / 255;
				row[x] = 0xFF000000u | (std::min(dr, 255u) << 16) | (std::min(dg, 255u) << 8) | std::min(db, 255u);
			}
		}
	}
	Statistics.DrawCalls++;
}


void UIRmlSoftRenderClass::RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation, Rml::TextureHandle texture)
{
	if (!IsReady || handle == 0) {
		return;
	}
	if (!std::isfinite(translation.x) || !std::isfinite(translation.y)) {
		Fail("a fragment's translation is not finite");
		return;
	}

	std::vector<ScreenVertex> vertices;
	Transform_Vertices(handle, translation, vertices);
	Texture const * sampled = (texture != 0) ? (Texture const *)texture : nullptr;
	Draw_Triangles(vertices, ((SoftGeometry const *)handle)->Indices, sampled, DRAW_COLOUR, 0);
}


Rml::TextureHandle UIRmlSoftRenderClass::LoadTexture(Rml::Vector2i & dimensions, Rml::String const & source)
{
	std::vector<unsigned char> rgba;
	int width = 0;
	int height = 0;

	UIImageResult result = UI_Load_Image(source.c_str(), rgba, width, height, true);

	if (result == UI_IMAGE_MISSING) {
		const unsigned int clear = 0;
		rgba.assign((unsigned char const *)&clear, (unsigned char const *)&clear + sizeof(clear));
		width = 1;
		height = 1;
	} else if (result != UI_IMAGE_LOADED) {
		char message[320];
		std::snprintf(message, sizeof(message), "%s did not decode", source.c_str());
		Fail(message);
		return(0);
	}

	dimensions.x = width;
	dimensions.y = height;
	if (ArtMagnification > 1) {
		std::vector<unsigned char> magnified;
		if (!UI_Render_Magnify_RGBA(std::span<std::uint8_t const>(rgba.data(), rgba.size()), width, height, ArtMagnification, magnified)) {
			Fail("a picture could not be magnified");
			return(0);
		}
		return(GenerateTexture(Rml::Span<const Rml::byte>(magnified.data(), magnified.size()), Rml::Vector2i(width * ArtMagnification, height * ArtMagnification)));
	}
	return(GenerateTexture(Rml::Span<const Rml::byte>(rgba.data(), rgba.size()), dimensions));
}


Rml::TextureHandle UIRmlSoftRenderClass::GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i dimensions)
{
	if (!IsReady) {
		return(0);
	}
	if (dimensions.x <= 0 || dimensions.y <= 0 || dimensions.x > UI_MAX_TEXTURE_DIMENSION || dimensions.y > UI_MAX_TEXTURE_DIMENSION) {
		Fail("a texture is empty or larger on a side than the renderer accepts");
		return(0);
	}
	size_t const count = (size_t)dimensions.x * (size_t)dimensions.y;
	if (source.size() != count * 4) {
		Fail("a texture's pixels do not match its dimensions");
		return(0);
	}

	Texture * texture = new Texture;
	texture->Width = dimensions.x;
	texture->Height = dimensions.y;
	texture->Pixels.resize(count);
	for (size_t index = 0; index < count; index++) {
		Rml::byte const * p = &source[index * 4];
		texture->Pixels[index] = (std::uint32_t)p[0] | ((std::uint32_t)p[1] << 8) | ((std::uint32_t)p[2] << 16) | ((std::uint32_t)p[3] << 24);
	}
	Statistics.TextureCount++;
	Statistics.TextureBytes += count * 4;
	return((Rml::TextureHandle)texture);
}


void UIRmlSoftRenderClass::ReleaseTexture(Rml::TextureHandle handle)
{
	if (handle == 0) {
		return;
	}
	Texture * texture = (Texture *)handle;
	Statistics.TextureCount--;
	Statistics.TextureBytes -= texture->Pixels.size() * 4;
	delete texture;
}


void UIRmlSoftRenderClass::EnableScissorRegion(bool enable)
{
	ScissorEnabled = enable;
}


void UIRmlSoftRenderClass::SetScissorRegion(Rml::Rectanglei region)
{
	Scissor = region;
}


void UIRmlSoftRenderClass::SetTransform(Rml::Matrix4f const * transform)
{
	TransformEnabled = false;
	if (transform == nullptr) {
		return;
	}
	float const * values = transform->data();
	for (int index = 0; index < 16; index++) {
		if (!std::isfinite(values[index])) {
			Fail("a transform is not finite");
			return;
		}
	}
	std::memcpy(Transform, values, sizeof(Transform));
	TransformEnabled = true;
}


void UIRmlSoftRenderClass::EnableClipMask(bool enable)
{
	ClipMaskEnabled = enable;
}


void UIRmlSoftRenderClass::RenderToClipMask(Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle handle, Rml::Vector2f translation)
{
	if (!IsReady || handle == 0) {
		return;
	}

	UIRenderMaskStep step;
	if (!UI_Render_Mask_Step(Mask_Operation(operation), StencilReference, step)) {
		Fail("clip masks nest deeper than the stencil counts");
		return;
	}

	Ensure_Stencil();
	if (step.Clear) {
		std::fill(Stencil.begin(), Stencil.end(), 0);
	}

	std::vector<ScreenVertex> vertices;
	Transform_Vertices(handle, translation, vertices);
	Draw_Triangles(vertices, ((SoftGeometry const *)handle)->Indices, nullptr, step.Increment ? DRAW_MASK_INCREMENT : DRAW_MASK_WRITE, step.Write);
	StencilReference = step.Reference;
}
