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

class ThemeManager : public QObject {
    Q_OBJECT
public:
    static ThemeManager& instance();

    void inicializar();
    UiTheme currentTheme() const { return m_theme; }
    bool isLight() const { return m_theme == UiTheme::Light; }
    bool isDark() const { return m_theme == UiTheme::Dark; }

    void setTheme(UiTheme theme);
    void applyWindowChrome(QWidget* widget) const;
    void setWindowTransitionsDisabled(QWidget* widget, bool disabled) const;
    void setWindowCloaked(QWidget* widget, bool cloaked) const;
    void applyAllTopLevelWindowChrome() const;
    void paintSpinArrowButton(QPainter* painter,
                              const QRect& rect,
                              Qt::ArrowType arrowType,
                              bool enabled,
                              bool hovered,
                              bool pressed) const;

signals:
    void themeChanged(UiTheme newTheme);

private:
    explicit ThemeManager(QObject* parent = nullptr);

    void loadSavedTheme();
    void saveTheme() const;
    void applyApplicationStyle() const;

    UiTheme m_theme = UiTheme::Dark;
    bool m_initialized = false;
};

} // namespace omniframe

