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


// a * b / 255, rounded, for bytes.
static inline unsigned Mul255(unsigned a, unsigned b)
{
	unsigned const t = a * b + 128;
	return((t + (t >> 8)) >> 8);
}

// A premultiplied colour over a layer pixel, which keeps 255 - a of what is there.
static inline std::uint32_t Over(std::uint32_t dest, unsigned r, unsigned g, unsigned b, unsigned a, int rs, int bs)
{
	unsigned const keep = 255 - a;
	unsigned const dr = std::min(r + Mul255((dest >> rs) & 0xFF, keep), 255u);
	unsigned const dg = std::min(g + Mul255((dest >> 8) & 0xFF, keep), 255u);
	unsigned const db = std::min(b + Mul255((dest >> bs) & 0xFF, keep), 255u);
	unsigned const da = std::min(a + Mul255(dest >> 24, keep), 255u);
	return((da << 24) | (dr << rs) | (dg << 8) | (db << bs));
}


// Over for a premultiplied pixel already in the layer's layout, two channels to a multiply: the same
// result as Over, which it falls back to when a channel would pass 255.
static inline std::uint32_t Over_Packed(std::uint32_t dest, std::uint32_t pixel, int rs, int bs)
{
	unsigned const keep = 255 - (pixel >> 24);
	std::uint32_t rb = (dest & 0x00FF00FFu) * keep + 0x00800080u;
	std::uint32_t ag = ((dest >> 8) & 0x00FF00FFu) * keep + 0x00800080u;
	rb = ((rb + ((rb >> 8) & 0x00FF00FFu)) >> 8) & 0x00FF00FFu;
	ag = ((ag + ((ag >> 8) & 0x00FF00FFu)) >> 8) & 0x00FF00FFu;
	rb += pixel & 0x00FF00FFu;
	ag += (pixel >> 8) & 0x00FF00FFu;
	if (((rb | ag) & 0x01000100u) != 0) {
		return(Over(dest, (pixel >> rs) & 0xFF, (pixel >> 8) & 0xFF, (pixel >> bs) & 0xFF, pixel >> 24, rs, bs));
	}
	return(rb | (ag << 8));
}


// The UI's layer and the renderer drawing it.
static SoftLayer _Layer;
static std::vector<std::uint32_t> _LayerPixels;
static int _LayerRedShift = -1;
static int _LayerBlueShift = -1;
static UIRmlSoftRenderClass * _Active = nullptr;

static SoftScreen Layer_Target(void)
{
	SoftScreen target;
	target.Pixels = _Layer.Pixels;
	target.Width = _Layer.Width;
	target.Height = _Layer.Height;
	target.Pitch = _Layer.Pitch;
	target.RedShift = _LayerRedShift;
	target.BlueShift = _LayerBlueShift;
	return(target);
}

// Sizes the layer to the screen; a new layer is clear. Returns false if it was already the screen's.
static bool Ensure_Layer(void)
{
	SoftScreen const & screen = Soft_Screen();
	if (_Layer.Width == screen.Width && _Layer.Height == screen.Height && _LayerRedShift == screen.RedShift && _LayerBlueShift == screen.BlueShift) {
		return(false);
	}
	_LayerPixels.assign((size_t)std::max(screen.Width, 0) * (size_t)std::max(screen.Height, 0), 0);
	_Layer.Pixels = _LayerPixels.empty() ? nullptr : _LayerPixels.data();
	_Layer.Width = screen.Width;
	_Layer.Height = screen.Height;
	_Layer.Pitch = screen.Width;
	_Layer.X0 = _Layer.Y0 = _Layer.X1 = _Layer.Y1 = 0;
	_Layer.Version++;
	_Layer.PartValid = false;
	_LayerRedShift = screen.RedShift;
	_LayerBlueShift = screen.BlueShift;
	return(true);
}

static void Clear_Layer(void)
{
	for (int y = _Layer.Y0; y < _Layer.Y1; y++) {
		std::uint32_t * row = _Layer.Pixels + (size_t)y * (size_t)_Layer.Pitch;
		std::fill(row + _Layer.X0, row + _Layer.X1, 0u);
	}
	_Layer.X0 = _Layer.Y0 = _Layer.X1 = _Layer.Y1 = 0;
	_Layer.Version++;
	_Layer.PartValid = false;
}

// Only the box from x0,y0 to x1,y1 has been drawn again.
static void Soft_UI_Layer_Changed(int x0, int y0, int x1, int y1)
{
	if (x1 <= x0 || y1 <= y0) {
		return;
	}
	_Layer.PartFrom = _Layer.Version;
	_Layer.Version++;
	_Layer.PartValid = true;
	_Layer.PartX0 = x0;
	_Layer.PartY0 = y0;
	_Layer.PartX1 = x1;
	_Layer.PartY1 = y1;
}

static void Grow_Layer_Box(int x0, int y0, int x1, int y1)
{
	if (x1 <= x0 || y1 <= y0) {
		return;
	}
	if (_Layer.X1 <= _Layer.X0 || _Layer.Y1 <= _Layer.Y0) {
		_Layer.X0 = x0;
		_Layer.Y0 = y0;
		_Layer.X1 = x1;
		_Layer.Y1 = y1;
		return;
	}
	_Layer.X0 = std::min(_Layer.X0, x0);
	_Layer.Y0 = std::min(_Layer.Y0, y0);
	_Layer.X1 = std::max(_Layer.X1, x1);
	_Layer.Y1 = std::max(_Layer.Y1, y1);
}


SoftLayer const & Soft_UI_Layer(void)
{
	return(_Layer);
}


void Soft_UI_Finish_Frame(void)
{
	if (_Active != nullptr) {
		_Active->Finish_Frame();
	}
}


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
	Commands.clear();
	LastCommands.clear();
	Drawing = false;
	InFrame = false;
	LayerStale = true;
	if (_Active == this) {
		_Active = nullptr;
		Clear_Layer();
	}
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

	_Active = this;
	int const view[4] = {x, y, width, height};
	if (Ensure_Layer() || InFrame || !std::equal(view, view + 4, LastView)) {
		LayerStale = true;
		std::copy(view, view + 4, LastView);
	}
	Commands.clear();
	Drawing = false;
	Differs = false;
	InFrame = true;
}


void UIRmlSoftRenderClass::Finish_Frame(void)
{
	if (!InFrame) {
		// Nothing was rendered this frame, so the layer is to be clear.
		Commands.clear();
		Drawing = false;
	}
	InFrame = false;
	bool const changed = Differs || Commands.size() != LastCommands.size();
	if (!Drawing && (LayerStale || (changed && !Redraw_Part()))) {
		Redraw_Commands_So_Far();
	}
	if (changed && !Drawing) {
		LastCommands.swap(Commands);
	}
	if (Drawing) {
		LastCommands.swap(Commands);
		LayerStale = false;
	}
	Commands.clear();
	Drawing = false;
	Differs = false;
}


bool UIRmlSoftRenderClass::Command::operator==(Command const & other) const
{
	return(!Dead && !other.Dead && Mask == other.Mask && Operation == other.Operation && Geometry == other.Geometry
		&& Translation.x == other.Translation.x && Translation.y == other.Translation.y && Texture == other.Texture
		&& ScissorEnabled == other.ScissorEnabled && (!ScissorEnabled || Scissor == other.Scissor)
		&& TransformEnabled == other.TransformEnabled && (!TransformEnabled || std::memcmp(Transform, other.Transform, sizeof(Transform)) == 0)
		&& ClipMaskEnabled == other.ClipMaskEnabled);
}


UIRmlSoftRenderClass::Command UIRmlSoftRenderClass::Current_Command(void) const
{
	Command command;
	command.ScissorEnabled = ScissorEnabled;
	command.Scissor = Scissor;
	command.TransformEnabled = TransformEnabled;
	std::memcpy(command.Transform, Transform, sizeof(Transform));
	command.ClipMaskEnabled = ClipMaskEnabled;
	return(command);
}


void UIRmlSoftRenderClass::Submit(Command const & command)
{
	Soft_Note_UI_Drawn();
	Commands.push_back(command);
	if (Drawing) {
		Execute(Commands.back());
		return;
	}
	size_t const index = Commands.size() - 1;
	if (!LayerStale) {
		if (index < LastCommands.size() && LastCommands[index] == command) {
			std::copy(LastCommands[index].Box, LastCommands[index].Box + 4, Commands.back().Box);
		} else {
			Differs = true;
		}
		return;
	}
	Redraw_Commands_So_Far();
}


/*
** Draws again only the box covering the commands that differ from the last frame's, as they
** were and as they are, if that is a small part of the layer. The commands the two frames start
** and end with alike are left out, so a command put in or taken out (a highlight under the
** pointer) counts as one that differs. A clip mask that differs can change what is drawn
** anywhere, so then it's the whole layer. Returns false if it didn't.
*/
bool UIRmlSoftRenderClass::Redraw_Part(void)
{
	Command const live = Current_Command();
	auto restore = [&]() {
		ScissorEnabled = live.ScissorEnabled;
		Scissor = live.Scissor;
		TransformEnabled = live.TransformEnabled;
		std::memcpy(Transform, live.Transform, sizeof(Transform));
		ClipMaskEnabled = live.ClipMaskEnabled;
	};
	int box[4] = {0, 0, 0, 0};
	auto add = [&](int const * other) {
		if (other[2] <= other[0] || other[3] <= other[1]) {
			return;
		}
		if (box[2] <= box[0] || box[3] <= box[1]) {
			std::copy(other, other + 4, box);
			return;
		}
		box[0] = std::min(box[0], other[0]);
		box[1] = std::min(box[1], other[1]);
		box[2] = std::max(box[2], other[2]);
		box[3] = std::max(box[3], other[3]);
	};
	size_t const count = Commands.size(), lastcount = LastCommands.size();
	size_t first = 0;
	while (first < count && first < lastcount && LastCommands[first] == Commands[first]) {
		first++;
	}
	size_t same_end = 0;
	while (same_end < count - first && same_end < lastcount - first
		&& LastCommands[lastcount - 1 - same_end] == Commands[count - 1 - same_end]) {
		Command const & last = LastCommands[lastcount - 1 - same_end];
		std::copy(last.Box, last.Box + 4, Commands[count - 1 - same_end].Box);
		same_end++;
	}
	for (size_t i = first; i < lastcount - same_end; i++) {
		if (LastCommands[i].Mask) {
			return(false);
		}
		add(LastCommands[i].Box);
	}
	std::vector<ScreenVertex> vertices;
	for (size_t i = first; i < count - same_end; i++) {
		Command & command = Commands[i];
		if (command.Mask) {
			restore();
			return(false);
		}
		ScissorEnabled = command.ScissorEnabled;
		Scissor = command.Scissor;
		TransformEnabled = command.TransformEnabled;
		std::memcpy(Transform, command.Transform, sizeof(Transform));
		Transform_Vertices(command.Geometry, command.Translation, vertices);
		Find_Box(command, vertices);
		add(command.Box);
	}
	restore();

	long long const area = (long long)(box[2] - box[0]) * (box[3] - box[1]);
	if (box[2] > box[0] && box[3] > box[1] && area * 2 > (long long)_Layer.Width * _Layer.Height) {
		return(false);
	}
	if (area > 0) {
		for (int y = box[1]; y < box[3]; y++) {
			std::uint32_t * row = _Layer.Pixels + (size_t)y * (size_t)_Layer.Pitch;
			std::fill(row + box[0], row + box[2], 0u);
		}
		Restricted = true;
		std::copy(box, box + 4, Restrict);
		StencilReference = 0;
		for (Command & command : Commands) {
			Execute(command);
		}
		Restricted = false;
		restore();
	}
	if (area > 0) {
		Soft_UI_Layer_Changed(box[0], box[1], box[2], box[3]);
	}
	return(true);
}


// The pixels a command can touch: its vertices' bounds, a pixel wider, within its clip.
void UIRmlSoftRenderClass::Find_Box(Command & command, std::vector<ScreenVertex> const & vertices) const
{
	std::fill(command.Box, command.Box + 4, 0);
	int clipx0, clipy0, clipx1, clipy1;
	if (vertices.empty() || !Clip_Rect(clipx0, clipy0, clipx1, clipy1)) {
		return;
	}
	float minx = vertices[0].X, maxx = minx, miny = vertices[0].Y, maxy = miny;
	for (ScreenVertex const & v : vertices) {
		minx = std::min(minx, v.X);
		maxx = std::max(maxx, v.X);
		miny = std::min(miny, v.Y);
		maxy = std::max(maxy, v.Y);
	}
	if (!std::isfinite(minx) || !std::isfinite(maxx) || !std::isfinite(miny) || !std::isfinite(maxy)) {
		int const whole[4] = {clipx0, clipy0, clipx1, clipy1};
		std::copy(whole, whole + 4, command.Box);
		return;
	}
	auto pixel = [](float value) { return((int)std::clamp(value, -1.0e6f, 1.0e6f)); };
	int const box[4] = {std::max(clipx0, pixel(std::floor(minx)) - 1), std::max(clipy0, pixel(std::floor(miny)) - 1),
		std::min(clipx1, pixel(std::ceil(maxx)) + 1), std::min(clipy1, pixel(std::ceil(maxy)) + 1)};
	if (box[2] > box[0] && box[3] > box[1]) {
		std::copy(box, box + 4, command.Box);
	}
}


// Clears the layer and draws this frame's commands so far; the rest of the frame is drawn as it comes.
void UIRmlSoftRenderClass::Redraw_Commands_So_Far(void)
{
	Command const live = Current_Command();
	Drawing = true;
	Clear_Layer();
	StencilReference = 0;
	for (Command & command : Commands) {
		Execute(command);
	}
	ScissorEnabled = live.ScissorEnabled;
	Scissor = live.Scissor;
	TransformEnabled = live.TransformEnabled;
	std::memcpy(Transform, live.Transform, sizeof(Transform));
	ClipMaskEnabled = live.ClipMaskEnabled;
}


void UIRmlSoftRenderClass::Execute(Command & command)
{
	ScissorEnabled = command.ScissorEnabled;
	Scissor = command.Scissor;
	TransformEnabled = command.TransformEnabled;
	std::memcpy(Transform, command.Transform, sizeof(Transform));
	ClipMaskEnabled = command.ClipMaskEnabled;

	std::vector<ScreenVertex> vertices;
	Transform_Vertices(command.Geometry, command.Translation, vertices);
	std::vector<int> const & indices = ((SoftGeometry const *)command.Geometry)->Indices;
	if (!Restricted) {
		Find_Box(command, vertices);
	}

	if (!command.Mask) {
		Texture const * sampled = (command.Texture != 0) ? (Texture const *)command.Texture : nullptr;
		Draw_Triangles(vertices, indices, sampled, DRAW_COLOUR, 0);
		return;
	}

	UIRenderMaskStep step;
	if (!UI_Render_Mask_Step(Mask_Operation(command.Operation), StencilReference, step)) {
		Fail("clip masks nest deeper than the stencil counts");
		return;
	}
	Ensure_Stencil();
	if (step.Clear) {
		std::fill(Stencil.begin(), Stencil.end(), 0);
	}
	Draw_Triangles(vertices, indices, nullptr, step.Increment ? DRAW_MASK_INCREMENT : DRAW_MASK_WRITE, step.Write);
	StencilReference = step.Reference;
}


/*
** Before a geometry or texture goes: a command of this frame's that uses it must be drawn while
** it can be, and a later handle at the same address isn't this one, so the last frame's commands
** that used it match nothing.
*/
void UIRmlSoftRenderClass::Release_Handle(Rml::CompiledGeometryHandle geometry, Rml::TextureHandle texture)
{
	auto uses = [&](Command const & command) {
		return((geometry != 0 && command.Geometry == geometry) || (texture != 0 && command.Texture == texture));
	};
	if (std::any_of(Commands.begin(), Commands.end(), uses)) {
		if (InFrame && !Drawing) {
			Redraw_Commands_So_Far();
		}
		LayerStale = true;
	}
	for (Command & command : LastCommands) {
		if (uses(command)) {
			command.Dead = true;
		}
	}
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
	Release_Handle(handle, 0);
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
	if (Restricted) {
		x0 = std::max(x0, Restrict[0]);
		y0 = std::max(y0, Restrict[1]);
		x1 = std::min(x1, Restrict[2]);
		y1 = std::min(y1, Restrict[3]);
	}
	return(x1 > x0 && y1 > y0);
}





/*
** The fast path for the commonest shape the UI draws: an axis-aligned rectangle, made of two
** triangles over four corners, of one colour, with its texture (if any) mapped straight
** across it. Boxes, images and glyphs are all of this kind. It covers the same pixels as the
** triangle rasteriser (pixel centres inside, left and top edges included) and blends the
** same way, but steps in integers. Returns false if the six indices are not such a quad.
*/
static bool Draw_Quad(SoftScreen const & screen, int clipx0, int clipy0, int clipx1, int clipy1,
	UIRmlSoftRenderClass::ScreenVertex const * vertices, int const * six, UIRmlSoftRenderClass::Texture const * texture)
{
	int const rs = screen.RedShift, bs = screen.BlueShift;
	int corner[4];
	int count = 0;
	for (int i = 0; i < 6; i++) {
		int const index = six[i];
		bool seen = false;
		for (int j = 0; j < count; j++) {
			seen = seen || corner[j] == index;
		}
		if (!seen) {
			if (count == 4) {
				return(false);
			}
			corner[count++] = index;
		}
	}
	if (count != 4) {
		return(false);
	}

	UIRmlSoftRenderClass::ScreenVertex const & first = vertices[corner[0]];
	float minx = first.X, maxx = first.X, miny = first.Y, maxy = first.Y;
	for (int j = 1; j < 4; j++) {
		minx = std::min(minx, vertices[corner[j]].X);
		maxx = std::max(maxx, vertices[corner[j]].X);
		miny = std::min(miny, vertices[corner[j]].Y);
		maxy = std::max(maxy, vertices[corner[j]].Y);
	}
	if (!(maxx > minx) || !(maxy > miny) || !std::isfinite(minx) || !std::isfinite(maxx) || !std::isfinite(miny) || !std::isfinite(maxy)) {
		return(false);
	}

	// Every corner on the box, all four corners present, one colour, and the texture
	// coordinates depending only on x (u) and only on y (v).
	float u0 = 0.0f, u1 = 0.0f, v0 = 0.0f, v1 = 0.0f;
	unsigned seenmask = 0;
	for (int j = 0; j < 4; j++) {
		UIRmlSoftRenderClass::ScreenVertex const & v = vertices[corner[j]];
		bool const left = v.X == minx, top = v.Y == miny;
		if ((!left && v.X != maxx) || (!top && v.Y != maxy)) {
			return(false);
		}
		if (v.R != first.R || v.G != first.G || v.B != first.B || v.A != first.A) {
			return(false);
		}
		unsigned const bit = 1u << ((left ? 0 : 1) + (top ? 0 : 2));
		if (seenmask & bit) {
			return(false);
		}
		seenmask |= bit;
	}
	if (seenmask != 15) {
		return(false);
	}
	bool haveu0 = false, haveu1 = false, havev0 = false, havev1 = false;
	for (int j = 0; j < 4; j++) {
		UIRmlSoftRenderClass::ScreenVertex const & v = vertices[corner[j]];
		float & u = (v.X == minx) ? u0 : u1;
		bool & haveu = (v.X == minx) ? haveu0 : haveu1;
		if (haveu && u != v.U) return(false);
		u = v.U; haveu = true;
		float & w = (v.Y == miny) ? v0 : v1;
		bool & havev = (v.Y == miny) ? havev0 : havev1;
		if (havev && w != v.V) return(false);
		w = v.V; havev = true;
	}

	// The two triangles must cover the box between them, rather than overlap in half of it.
	auto twice_area = [&](int a, int b, int c) {
		UIRmlSoftRenderClass::ScreenVertex const & p = vertices[a], & q = vertices[b], & r = vertices[c];
		return(std::fabs((q.X - p.X) * (r.Y - p.Y) - (r.X - p.X) * (q.Y - p.Y)));
	};
	float const box = (maxx - minx) * (maxy - miny);
	float const covered = twice_area(six[0], six[1], six[2]) + twice_area(six[3], six[4], six[5]);
	if (std::fabs(covered - 2.0f * box) > 0.01f * box) {
		return(false);
	}

	int x0 = std::max(clipx0, (int)std::ceil(minx - 0.5f));
	int x1 = std::min(clipx1, (int)std::ceil(maxx - 0.5f));
	int y0 = std::max(clipy0, (int)std::ceil(miny - 0.5f));
	int y1 = std::min(clipy1, (int)std::ceil(maxy - 0.5f));
	if (x1 <= x0 || y1 <= y0) {
		return(true);
	}

	unsigned const cr = first.R, cg = first.G, cb = first.B, ca = first.A;
	bool const white = cr == 255 && cg == 255 && cb == 255 && ca == 255;

	if (texture == nullptr) {
		if (ca == 0 && cr == 0 && cg == 0 && cb == 0) {
			return(true);
		}
		std::uint32_t const solid = 0xFF000000u | (cr << rs) | (cg << 8) | (cb << bs);
		for (int y = y0; y < y1; y++) {
			std::uint32_t * row = screen.Pixels + (size_t)y * (size_t)screen.Pitch;
			if (ca == 255) {
				for (int x = x0; x < x1; x++) row[x] = solid;
			} else {
				std::uint32_t const pixel = (ca << 24) | (cr << rs) | (cg << 8) | (cb << bs);
				for (int x = x0; x < x1; x++) {
					row[x] = Over_Packed(row[x], pixel, rs, bs);
				}
			}
		}
		return(true);
	}

	// Texel coordinates in 16.16, stepped per pixel.
	int const tw = texture->Width, th = texture->Height;
	double const dudx = (double)(u1 - u0) * tw / (double)(maxx - minx);
	double const dvdy = (double)(v1 - v0) * th / (double)(maxy - miny);
	std::int32_t const ustep = (std::int32_t)std::lround(dudx * 65536.0);
	std::int32_t const ustart = (std::int32_t)std::floor(((double)u0 * tw + ((double)x0 + 0.5 - minx) * dudx) * 65536.0);
	std::uint32_t const * pixels = texture->Pixels.data();
	bool const swap = rs != 0;

	for (int y = y0; y < y1; y++) {
		int ty = (int)std::floor((double)v0 * th + ((double)y + 0.5 - miny) * dvdy);
		ty = ty < 0 ? 0 : (ty >= th ? th - 1 : ty);
		std::uint32_t const * texrow = pixels + (size_t)ty * (size_t)tw;
		std::uint32_t * row = screen.Pixels + (size_t)y * (size_t)screen.Pitch;
		std::int32_t u = ustart;
		for (int x = x0; x < x1; x++, u += ustep) {
			int tx = u >> 16;
			tx = tx < 0 ? 0 : (tx >= tw ? tw - 1 : tx);
			std::uint32_t texel = texrow[tx];
			if (!white) {
				texel = Mul255(texel & 0xFF, cr) | (Mul255((texel >> 8) & 0xFF, cg) << 8)
					| (Mul255((texel >> 16) & 0xFF, cb) << 16) | (Mul255(texel >> 24, ca) << 24);
			}
			if (texel == 0) {
				continue;
			}
			// Textures are red first; the layer may want blue there.
			std::uint32_t const pixel = swap ? (texel & 0xFF00FF00u) | ((texel & 0xFFu) << 16) | ((texel >> 16) & 0xFFu) : texel;
			row[x] = (pixel >> 24) == 255 ? pixel : Over_Packed(row[x], pixel, rs, bs);
		}
	}
	return(true);
}

static inline bool Top_Left(float ax, float ay, float bx, float by)
{
	// With the triangle wound clockwise on screen (y down), a top edge runs exactly left to
	// right and a left edge runs upwards.
	return((ay == by && bx > ax) || (by < ay));
}


void UIRmlSoftRenderClass::Draw_Triangles(std::vector<ScreenVertex> const & vertices, std::vector<int> const & indices, Texture const * texture, DrawKind kind, std::uint8_t value)
{
	SoftScreen const screen = Layer_Target();
	if (screen.Pixels == nullptr) {
		return;
	}
	int const rs = screen.RedShift, bs = screen.BlueShift;

	int clipx0, clipy0, clipx1, clipy1;
	if (!Clip_Rect(clipx0, clipy0, clipx1, clipy1)) {
		return;
	}
	if (kind == DRAW_COLOUR && !vertices.empty()) {
		float minx = vertices[0].X, maxx = minx, miny = vertices[0].Y, maxy = miny;
		for (ScreenVertex const & v : vertices) {
			minx = std::min(minx, v.X);
			maxx = std::max(maxx, v.X);
			miny = std::min(miny, v.Y);
			maxy = std::max(maxy, v.Y);
		}
		if (!std::isfinite(minx) || !std::isfinite(maxx) || !std::isfinite(miny) || !std::isfinite(maxy)) {
			Grow_Layer_Box(clipx0, clipy0, clipx1, clipy1);
		} else {
			auto pixel = [](float value) { return((int)std::clamp(value, -1.0e6f, 1.0e6f)); };
			Grow_Layer_Box(std::max(clipx0, pixel(std::floor(minx)) - 1), std::max(clipy0, pixel(std::floor(miny)) - 1),
				std::min(clipx1, pixel(std::ceil(maxx)) + 1), std::min(clipy1, pixel(std::ceil(maxy)) + 1));
		}
	}

	bool const test_mask = (kind == DRAW_COLOUR) && ClipMaskEnabled;
	if (kind != DRAW_COLOUR || test_mask) {
		Ensure_Stencil();
	}

	// Quads take the fast path; only what is left goes through the general rasteriser.
	bool const quads = kind == DRAW_COLOUR && !test_mask && !NoFastQuads;
	for (size_t t = 0; t + 2 < indices.size(); t += 3) {
		if (quads && (t % 6) == 0 && t + 5 < indices.size()
			&& Draw_Quad(screen, clipx0, clipy0, clipx1, clipy1, vertices.data(), &indices[t], texture)) {
			t += 3;
			continue;
		}
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
				unsigned const dr = r + (((dest >> rs) & 0xFF) * keep + 127) / 255;
				unsigned const dg = g + (((dest >> 8) & 0xFF) * keep + 127) / 255;
				unsigned const db = bl + (((dest >> bs) & 0xFF) * keep + 127) / 255;
				unsigned const da = al + ((dest >> 24) * keep + 127) / 255;
				row[x] = (std::min(da, 255u) << 24) | (std::min(dr, 255u) << rs) | (std::min(dg, 255u) << 8) | (std::min(db, 255u) << bs);
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

	Command command = Current_Command();
	command.Geometry = handle;
	command.Translation = translation;
	command.Texture = texture;
	Submit(command);
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
	Release_Handle(0, handle);
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

	Command command = Current_Command();
	command.Mask = true;
	command.Operation = operation;
	command.Geometry = handle;
	command.Translation = translation;
	Submit(command);
}
