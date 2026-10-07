#include "Dialogs.h"

namespace Dialogs
{

void askText (const juce::String& title, const juce::String& message, const juce::String& initial,
              std::function<void (const juce::String&)> onOk, juce::Component* associated)
{
    auto* w = new juce::AlertWindow (title, message, juce::MessageBoxIconType::NoIcon, associated);
    w->addTextEditor ("text", initial);
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("キャンセル"_ju, 0, juce::KeyPress (juce::KeyPress::escapeKey));

    if (auto* ed = w->getTextEditor ("text"))
        ed->selectAll();

    w->enterModalState (true, juce::ModalCallbackFunction::create ([w, onOk = std::move (onOk)] (int result)
    {
        if (result == 1 && onOk)
            onOk (w->getTextEditorContents ("text").trim());
    }), true);

    // すぐに入力できるように入力欄にフォーカスを移す
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<juce::AlertWindow> (w)]
    {
        if (safe != nullptr)
            if (auto* ed = safe->getTextEditor ("text"))
            {
                safe->toFront (true);
                ed->grabKeyboardFocus();
                ed->selectAll();
            }
    });
}

void confirm (const juce::String& title, const juce::String& message, const juce::String& okText,
              std::function<void()> onOk, juce::Component* associated)
{
    auto options = juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::QuestionIcon,
                                                                  title, message, okText, "キャンセル"_ju, associated);
    juce::AlertWindow::showAsync (options, [onOk = std::move (onOk)] (int result)
    {
        if (result == 1 && onOk)
            onOk();
    });
}

void askChoice (const juce::String& title, const juce::String& message, const juce::StringArray& buttons,
                std::function<void (int)> onResult, juce::Component* associated)
{
    auto* window = new juce::AlertWindow (title, message, juce::MessageBoxIconType::QuestionIcon, associated);

    for (int i = 0; i < buttons.size(); ++i)
        window->addButton (buttons[i], i + 1, {}, i == buttons.size() - 1 ? juce::KeyPress (juce::KeyPress::escapeKey) : juce::KeyPress());

    for (int i = 0; i < window->getNumButtons(); ++i)
        if (auto* b = window->getButton (i))
            b->setWantsKeyboardFocus (false);

    window->enterModalState (true, juce::ModalCallbackFunction::create ([onResult = std::move (onResult)] (int result)
    {
        if (onResult)
            onResult (result);
    }), true);
}

void askSaveChanges (const juce::String& message, std::function<void (int)> onResult)
{
    auto options = juce::MessageBoxOptions::makeOptionsYesNoCancel (juce::MessageBoxIconType::QuestionIcon,
                                                                    "変更を保存しますか？"_ju, message,
                                                                    "保存する"_ju, "保存しない"_ju, "キャンセル"_ju);
    juce::AlertWindow::showAsync (options, [onResult = std::move (onResult)] (int result)
    {
        if (onResult)
            onResult (result);
    });
}

void showError (const juce::String& title, const juce::String& message)
{
    juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOk (juce::MessageBoxIconType::WarningIcon,
                                                                          title, message), nullptr);
}

void showInfo (const juce::String& title, const juce::String& message)
{
    juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOk (juce::MessageBoxIconType::InfoIcon,
                                                                          title, message), nullptr);
}

}
