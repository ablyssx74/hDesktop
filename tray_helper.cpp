// hdesktop's tray helper: an item in the Deskbar's tray, and so code that runs inside the Deskbar. When hdesktop asks,
// it lets each of the other tray views draw itself into a BPicture (their own Draw(), recorded instead of shown), plays
// the picture into an off-screen bitmap and sends the pixels back. That works with the Deskbar hidden too. It does
// nothing until it is asked, and it never creates, moves or deletes another view.
//
// On 32-bit Haiku the Deskbar is a gcc 2 program, so this file has to stay buildable with gcc 2.
#include <Bitmap.h>
#include <Dragger.h>
#include <Message.h>
#include <Picture.h>
#include <View.h>
#include <Window.h>
#include <string.h>

#include "tray_helper.h"

class TrayHelperView : public BView {
public:
	TrayHelperView(BRect frame)
		:
		BView(frame, HDESKTOP_TRAY_HELPER_NAME, B_FOLLOW_NONE, B_WILL_DRAW)
	{
	}

	TrayHelperView(BMessage* archive)
		:
		BView(archive)
	{
	}

	static BArchivable* Instantiate(BMessage* archive);

	virtual status_t Archive(BMessage* archive, bool deep = true) const
	{
		status_t status = BView::Archive(archive, deep);
		if (status != B_OK)
			return status;
		archive->AddString("add_on", HDESKTOP_TRAY_HELPER_SIGNATURE);
		return archive->AddString("class", "TrayHelperView");
	}

	virtual void AttachedToWindow()
	{
		BView::AttachedToWindow();
		// the item itself shows nothing: it has the color of the tray
		if (Parent() != NULL) {
			SetViewColor(Parent()->ViewColor());
			Invalidate();
		}
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what != kTrayHelperCapture) {
			BView::MessageReceived(message);
			return;
		}
		BMessage reply(kTrayHelperPictures);
		reply.AddInt32("version", kTrayHelperVersion);

		float scale = 1;
		if (message->FindFloat("scale", &scale) != B_OK || scale < 1 || scale > 4)
			scale = 1;
		const char* name;
		for (int32 i = 0; message->FindString("name", i, &name) == B_OK; i++)
			_Capture(name, scale, reply);
		message->SendReply(&reply);
	}

private:
	static int32 _Color(rgb_color color)
	{
		return ((int32)color.red << 16) | ((int32)color.green << 8) | color.blue;
	}

	void _Capture(const char* name, float scale, BMessage& reply)
	{
		BView* tray = Parent();
		if (tray == NULL)
			return;
		BView* view = NULL;
		for (int32 i = 0; i < tray->CountChildren(); i++) {
			BView* child = tray->ChildAt(i);
			if (child != this && dynamic_cast<BDragger*>(child) == NULL && child->Name() != NULL
				&& strcmp(child->Name(), name) == 0) {
				view = child;
				break;
			}
		}
		if (view == NULL || view->IsHidden(view))
			return;

		const BRect bounds = view->Bounds();
		const int32 width = (int32)bounds.Width() + 1;
		const int32 height = (int32)bounds.Height() + 1;
		if (width < 2 || height < 2 || width > 256 || height > 64)
			return;

		BPicture picture;
		view->PushState();
		view->BeginPicture(&picture);
		view->Draw(bounds);
		BPicture* recorded = view->EndPicture();
		view->PopState();
		if (recorded == NULL)
			return;

		const int32 outWidth = (int32)(width * scale);
		const int32 outHeight = (int32)(height * scale);
		uint8* pixels = _Play(recorded, bounds, outWidth, outHeight, scale);
		uint8* small = _Play(recorded, bounds, width, height, 1);
		if (pixels == NULL || small == NULL) {
			delete[] pixels;
			delete[] small;
			return;
		}

		reply.AddString("name", name);
		reply.AddInt32("width", outWidth);
		reply.AddInt32("height", outHeight);
		reply.AddInt32("back", _Color(tray->ViewColor()));
		reply.AddInt32("back", _Color(view->ViewColor()));
		reply.AddInt32("back", _Color(view->LowColor()));
		reply.AddData("pixels", B_RAW_TYPE, pixels, outWidth * outHeight * 4, false);
		reply.AddInt32("swidth", width);
		reply.AddInt32("sheight", height);
		reply.AddData("small", B_RAW_TYPE, small, width * height * 4, false);
		delete[] pixels;
		delete[] small;
	}

	// The picture played onto a black and onto a white ground: how much the two differ at a pixel says how see-through
	// the item is there (soft edges and shadows too). Returns B_RGBA32 rows without padding, or NULL.
	static uint8* _Play(BPicture* picture, BRect bounds, int32 width, int32 height, float scale)
	{
		BBitmap black(BRect(0, 0, width - 1, height - 1), B_BITMAP_ACCEPTS_VIEWS, B_RGBA32);
		BBitmap white(BRect(0, 0, width - 1, height - 1), B_BITMAP_ACCEPTS_VIEWS, B_RGBA32);
		if (!_PlayOn(black, picture, bounds, scale, 0) || !_PlayOn(white, picture, bounds, scale, 255))
			return NULL;
		uint8* result = new uint8[width * height * 4];
		for (int32 y = 0; y < height; y++) {
			const uint8* b = (const uint8*)black.Bits() + y * black.BytesPerRow();
			const uint8* w = (const uint8*)white.Bits() + y * white.BytesPerRow();
			uint8* out = result + y * width * 4;
			for (int32 x = 0; x < width; x++, b += 4, w += 4, out += 4) {
				int alpha = 255 - ((w[0] - b[0]) + (w[1] - b[1]) + (w[2] - b[2])) / 3;
				if (alpha > 255)
					alpha = 255;
				if (alpha < 3)
					alpha = 0;
				for (int c = 0; c < 3; c++) {
					int value = alpha == 0 ? 0 : (b[c] * 255 + alpha / 2) / alpha;
					out[c] = value > 255 ? 255 : value;
				}
				out[3] = alpha;
			}
		}
		return result;
	}

	static bool _PlayOn(BBitmap& bitmap, BPicture* picture, BRect bounds, float scale, uint8 ground)
	{
		if (bitmap.InitCheck() != B_OK)
			return false;
		uint8* bits = (uint8*)bitmap.Bits();
		memset(bits, ground, bitmap.BitsLength());
		for (int32 i = 3; i < (int32)bitmap.BitsLength(); i += 4)
			bits[i] = 255;
		BView* canvas = new BView(bitmap.Bounds(), "canvas", B_FOLLOW_NONE, B_WILL_DRAW);
		if (!bitmap.Lock()) {
			delete canvas;
			return false;
		}
		bitmap.AddChild(canvas);
		canvas->SetViewColor(B_TRANSPARENT_COLOR);
		canvas->SetScale(scale);
		canvas->SetOrigin(-bounds.left, -bounds.top);
		canvas->DrawPicture(picture, B_ORIGIN);
		canvas->Sync();
		bitmap.Unlock();
		return true;
	}
};


BArchivable*
TrayHelperView::Instantiate(BMessage* archive)
{
	return validate_instantiation(archive, "TrayHelperView") ? new TrayHelperView(archive) : NULL;
}


int
main()
{
	return 0;
}


extern "C" _EXPORT BView*
instantiate_deskbar_item(float maxWidth, float maxHeight)
{
	return new TrayHelperView(BRect(0, 0, 0, maxHeight - 1));
}
