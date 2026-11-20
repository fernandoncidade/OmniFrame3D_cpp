#pragma once

#include <QObject>

class QPainter;
class QRect;
class QWidget;

namespace omniframe {

enum class UiTheme {
    Dark = 0,
    Light
};

enum class UiStyleMode {
    Custom = 0,
    Native
};

class ThemeManager : public QObject {
    Q_OBJECT
public:
    static ThemeManager& instance();

    void inicializar();
    bool reloadSavedTheme();
    UiTheme currentTheme() const { return m_theme; }
    bool isLight() const { return m_theme == UiTheme::Light; }
    bool isDark() const { return m_theme == UiTheme::Dark; }

    UiStyleMode uiStyleMode() const { return m_uiStyleMode; }
    bool isNativeUi() const { return m_uiStyleMode == UiStyleMode::Native; }
    bool isCustomUi() const { return m_uiStyleMode == UiStyleMode::Custom; }

    void setTheme(UiTheme theme);
    void setUiStyleMode(UiStyleMode mode);
    void applyWindowChrome(QWidget* widget) const;
    void setWindowTransitionsDisabled(QWidget* widget, bool disabled) const;
    void setWindowCloaked(QWidget* widget, bool cloaked) const;
    void applyAllTopLevelWindowChrome() const;
    void refreshToolbarExtensionButtons() const;
    void paintSpinArrowButton(QPainter* painter,
                              const QRect& rect,
                              Qt::ArrowType arrowType,
                              bool enabled,
                              bool hovered,
                              bool pressed) const;

signals:
    void themeChanged(UiTheme newTheme);
    void uiStyleModeChanged(UiStyleMode newMode);

private:
    explicit ThemeManager(QObject* parent = nullptr);

    void loadSavedTheme();
    void saveTheme() const;
    void applyApplicationStyle() const;

    UiTheme m_theme = UiTheme::Dark;
    UiStyleMode m_uiStyleMode = UiStyleMode::Custom;
    bool m_initialized = false;
    mutable QString m_nativeStyleName;
    mutable bool m_customStyleApplied = false;
    mutable bool m_customPaletteApplied = false;
    mutable bool m_applyingStyle = false;
};

} // namespace omniframe

