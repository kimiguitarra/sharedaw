#pragma once

#include "Common.h"

/**
    コマンドのキー割り当て（KeyPressMappingSet）に渡す前に、テンキーのキーを直す KeyListener。

    Windows の JUCE は、テンキーの「.」「+」「-」「*」「/」を普通の文字のキー（'.' など）として送ってくるので、
    テンキー用の割り当て（テンキー . で先頭へ、+ / - で 1 小節移動など）に当たらない。
    そのキーが押されたとき、実際にテンキーのキーが押されていればテンキーのキーに置き換える。
*/
class NumpadKeys  : public juce::KeyListener
{
public:
    explicit NumpadKeys (juce::KeyPressMappingSet& m) : mappings (m) {}

    static juce::KeyPress normalise (const juce::KeyPress& key)
    {
        static const std::pair<int, int> keys[] = {
            { '.', juce::KeyPress::numberPadDecimalPoint }, { ',', juce::KeyPress::numberPadDecimalPoint },
            { '+', juce::KeyPress::numberPadAdd }, { '-', juce::KeyPress::numberPadSubtract },
            { '*', juce::KeyPress::numberPadMultiply }, { '/', juce::KeyPress::numberPadDivide },
        };

        for (auto [character, numpad] : keys)
            if (key.getKeyCode() == character && juce::KeyPress::isKeyCurrentlyDown (numpad))
                return juce::KeyPress (numpad, key.getModifiers(), key.getTextCharacter());

        return key;
    }

    bool keyPressed (const juce::KeyPress& key, juce::Component* origin) override
    {
        return mappings.keyPressed (normalise (key), origin);
    }

    bool keyStateChanged (bool isKeyDown, juce::Component* origin) override
    {
        return mappings.keyStateChanged (isKeyDown, origin);
    }

private:
    juce::KeyPressMappingSet& mappings;
};
