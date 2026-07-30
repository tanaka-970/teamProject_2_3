#pragma once
#include"Sprite.h"
#include<vector>
#include<memory>
#include<string>
#include <unordered_map>
#include <d3d11.h>

// You can use UI class to if Scene classe need this.

//画像のあるfilepathの保持と描画のみ担当
class UI
{
public:
	
	void Render(ID3D11DeviceContext* dc, sprite::AlphaMode mode);
	void SetSprite(std::shared_ptr<sprite> sprite){mSprite = sprite;}
	// 表示位置・サイズ
	void SetTransform(float dx, float dy, float dz, float dw, float dh)
	{
		mDx = dx; mDy = dy; mDz = dz; mDw = dw; mDh = dh;
	}
	// テクスチャ切り抜き（省略時は画像全体）
	void SetUV(float sx, float sy, float sw, float sh)
	{
		mSx = sx; mSy = sy; mSw = sw; mSh = sh; mUseFullUV = false;
	}
	void SetColor(float r, float g, float b, float a)
	{
		mR = r; mG = g; mB = b; mA = a;
	}
	void SetAngle(float angle) { mAngle = angle; }
	void SetAlphaMode(sprite::AlphaMode mode) 
	{
		if (mSprite) mSprite->SetAlphaMode(mode); 
	}
		
	
	sprite* GetSprite() { return mSprite.get(); }
private:
	std::shared_ptr<sprite> mSprite;
	float mDx = 0, mDy = 0, mDz = 0, mDw = 0, mDh = 0;
	float mSx = 1.0f, mSy = 1.0f, mSw = 1.0f, mSh = 1.0f;
	float mAngle = 0;
	float mR = 1, mG = 1, mB = 1, mA = 1;
	bool  mUseFullUV = true; // trueならSprite側の全体描画Renderを使う
};

//外部からUISpriteを追加,取得する際の窓口と、spriteの管理
class UIManager
{
public:
	void Initalize(ID3D11Device* device);

	std::vector<UI>& GetUI() { return mUIs; }
	void ClearAllUi() 
	{
		mUIs.clear();
		ClearUnusedSpriteCache();
	}
	sprite* searchSprite(const wchar_t* filepath)
	{
		auto it = mSpriteCache.find(filepath);
		if (it != mSpriteCache.end()) 
		{
			return it->second.get();
		}
	}
	//画像を追加する際に描画設定を行う
	void AddNewUI(const wchar_t* filepath,
		float dx, float dy, float dz, float dw, float dh,
		float angle,
		float r, float g, float b, float a);
private:

	std::shared_ptr<sprite> GetOrLoadSprite(const wchar_t* filepath);
	
	void ClearUnusedSpriteCache();
	ID3D11Device* mDevice = nullptr;
	std::vector<UI> mUIs;
	std::unordered_map<const wchar_t*, std::shared_ptr<sprite>> mSpriteCache;
};

