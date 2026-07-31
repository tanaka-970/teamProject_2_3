#include "UI.h"
#include<sprite.h>
#include<d3d11.h>
#include <string>
#include <crtdbg.h>
//白抜き->Sprite::AlphaMode::White
// 黒抜き->Sprite::AlphaMode::Black
//そのまま->Sprite::AlphaMode::UseTextureAlpha
void UI::Render(ID3D11DeviceContext* dc, sprite::AlphaMode mode)
{
	mSprite->SetAlphaMode(mode);
	if (mUseFullUV) 
	{

		mSprite->render(dc,mDx, mDy,mDw, mDh,mAngle, mR, mG, mB, mA);
	}
	else 
	{
		mSprite->render(dc,mDx, mDy,mDw, mDh,mSx, mSy, mSw, mSh,mAngle, mR, mG, mB, mA);
	}
		
}
void UIManager::AddNewUI(const wchar_t* filepath,
	float dx, float dy, float dz, float dw, float dh,
	float angle,
	float r, float g, float b, float a)
{
	
	//コードの不備でDeviceがnullptrだった場合の保険
	_ASSERTE(mDevice != nullptr);
	mUIs.emplace_back();
	mUIs.back().SetSprite(GetOrLoadSprite(filepath));
	mUIs.back().SetTransform(dx, dy, dz, dw, dh);
	mUIs.back().SetColor(r,g,b,a);
	mUIs.back().SetAngle(angle);

}

//Check already loaded filepath
std::shared_ptr<sprite> UIManager::GetOrLoadSprite(const wchar_t* filepath) 
{
	auto it = mSpriteCache.find(filepath);
	if (it != mSpriteCache.end())
	{
		return it->second; // キャッシュ済みならそれを返す
	}

	auto Sprite = std::make_shared<sprite>(mDevice, filepath);
	mSpriteCache[filepath] = Sprite;
	return Sprite;
}

void UIManager::ClearUnusedSpriteCache()
{
	for (auto it = mSpriteCache.begin(); it != mSpriteCache.end(); )
	{
		if (it->second.use_count() == 1) // キャッシュ以外に誰も参照していない
		{
			it = mSpriteCache.erase(it);
		}
		else
		{
			++it;
		}
	}
}
void UIManager::Initalize(ID3D11Device* device)
{
	mDevice = device;
}
