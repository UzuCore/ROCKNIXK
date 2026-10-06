// SPDX-License-Identifier: GPL-2.0-or-later
#include <QApplication>
#include <QCommandLineParser>
#include <QCloseEvent>
#include <QDir>
#include <QFrame>
#include <QFontDatabase>
#include <QGridLayout>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <SDL.h>
#include <array>
#include <functional>
#include <memory>
#include <cstdio>
#include <cstdlib>

const std::array<QString, 4> ids{"left-stick", "right-stick", "left-side", "right-side"};
const std::array<QString, 4> names{"왼쪽 스틱", "오른쪽 스틱", "왼쪽 측면", "오른쪽 측면"};
const std::array<QString, 4> effectIds{"steady", "slow", "normal", "fast"};
const std::array<QString, 4> effectNames{"계속 켜기", "느리게 깜빡임", "보통 깜빡임", "빠르게 깜빡임"};
const std::array<QString, 5> allEffectIds{"steady", "slow", "normal", "fast", "scan"};
const std::array<QString, 5> allEffectNames{"계속 켜기", "느리게 깜빡임", "보통 깜빡임", "빠르게 깜빡임", "좌우 왕복"};

// Shared brand colors from install_tool/winforms/Shared/BrandTheme.cs.
struct MenuPalette {
    QColor background{"#181818"};
    QColor title{"#F4F4F4"};
    QColor text{"#B8B8B8"};
    QColor selector{"#382425"};
    QColor selected{"#FF5555"};
    QColor border{"#555555"};
    QColor surface() const { return QColor("#242424"); }
    QColor button() const { return QColor("#353535"); }
};

class OdinDiagram : public QWidget {
public:
    std::array<bool, 4> states{};
    MenuPalette colors;
    explicit OdinDiagram(QWidget *parent = nullptr) : QWidget(parent) { setMinimumSize(300, 310); }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.translate(width() / 2.0, height() / 2.0);
        p.scale(width() / 350.0, width() / 350.0);
        p.setPen(QPen(colors.border, 2));
        p.setBrush(colors.surface());
        p.drawRoundedRect(QRectF(-155, -82, 310, 164), 32, 32);
        p.setPen(Qt::NoPen);
        p.setBrush(colors.background);
        p.drawRoundedRect(QRectF(-83, -67, 166, 134), 8, 8);
        p.setPen(colors.text);
        p.drawText(QRectF(-83, -40, 166, 80), Qt::AlignCenter, "ODIN");
        for (int i = 0; i < 4; ++i) {
            p.setPen(QPen(states[i] ? colors.selected : colors.border, states[i] ? 5 : 3));
            p.setBrush(colors.background);
            if (i < 2) p.drawEllipse(QPointF(i == 0 ? -117 : 117, i == 0 ? -37 : 37), 19, 19);
            else p.drawLine(QPointF(i == 2 ? -149 : 149, -46), QPointF(i == 2 ? -149 : 149, 46));
        }
        p.setPen(QPen(colors.text, 6));
        p.drawLine(QPointF(-128, 35), QPointF(-106, 35));
        p.drawLine(QPointF(-117, 24), QPointF(-117, 46));
        for (auto point : {QPointF(117, -48), QPointF(128, -37), QPointF(117, -26), QPointF(106, -37)})
            p.drawEllipse(point, 1.5, 1.5);
    }
};

class LedToggle : public QPushButton {
public:
    using QPushButton::QPushButton;
protected:
    void nextCheckState() override {} // Display only the confirmed hardware state.
};

class LedWindow : public QWidget {
public:
    bool preview;
    MenuPalette colors;
    bool pending = false;
    bool loaded = false;
    bool closing = false;
    int previewDelayMs = 0;
    int applied = 0;
    std::array<bool, 4> available{};
    std::array<bool, 4> states{};
    std::array<QPushButton *, 4> toggles{};
    std::array<QPushButton *, 4> effectButtons{};
    std::array<int, 4> effects{};
    std::array<bool, 4> blinkAvailable{};
    QString groupEffect = "individual";
    bool groupRunning = false;
    bool scanAvailable = false;
    QList<QPushButton *> navigation;
    QLabel *notice;
    OdinDiagram *diagram;
    QPushButton *allOn;
    QPushButton *allOff;
    QPushButton *allEffect;
    QProcess process;
    SDL_GameController *controller = nullptr;
    bool selectHeld = false;
    bool startHeld = false;
    std::array<int, 2> axisDirections{};
    QTimer controllerTimer;

    explicit LedWindow(bool demo) : preview(demo) {
        setWindowTitle("Odin LED 설정");
        setMinimumSize(900, 740);
        resize(1160, 860);
        setStyleSheet(QString(R"(
          QWidget { background:%1; color:%2; font-family:'Noto Sans CJK KR','Noto Sans CJK SC'; font-size:22px; }
          QLabel#brand { color:%7; font-size:17px; font-weight:700; }
          QLabel#heading { color:%2; font-size:38px; font-weight:700; }
          QLabel#subtitle { color:%3; font-size:19px; }
          QFrame#card { background:%4; border-radius:6px; }
          QFrame#card QLabel { background:transparent; }
          QLabel#cardtitle { color:%2; font-weight:700; }
          QLabel#description { color:%3; font-size:17px; }
          QPushButton { border:2px solid %8; border-radius:4px; background:%5; padding:14px 20px; min-height:32px; }
          QPushButton:checked { background:%6; color:%7; }
          QPushButton:focus { border:2px solid %7; background:%7; color:%1; }
          QPushButton:hover { border-color:%7; }
          QPushButton:disabled { color:#8A8A8A; background:%1; border-color:%4; }
          QPushButton#effect { font-size:18px; padding:8px 12px; min-height:24px; }
        )").arg(colors.background.name(), colors.title.name(), colors.text.name(), colors.surface().name(),
                 colors.button().name(), colors.selector.name(), colors.selected.name(), colors.border.name()));
        auto root = new QVBoxLayout(this);
        root->setContentsMargins(36, 28, 36, 26);
        root->setSpacing(12);
        auto brand = new QLabel(preview ? "ROCKNIXK · 미리보기" : "ROCKNIXK");
        brand->setObjectName("brand");
        auto heading = new QLabel("LED 설정");
        heading->setObjectName("heading");
        auto subtitle = new QLabel("스틱·측면 조명은 파란색 고정입니다. 켜기와 깜빡임을 설정하세요.");
        subtitle->setObjectName("subtitle");
        root->addWidget(brand);
        root->addWidget(heading);
        root->addWidget(subtitle);
        auto body = new QHBoxLayout;
        diagram = new OdinDiagram;
        body->addWidget(diagram, 3);
        auto grid = new QGridLayout;
        grid->setSpacing(16);
        for (int i = 0; i < 4; ++i) {
            auto card = new QFrame;
            card->setObjectName("card");
            card->setMinimumHeight(180);
            auto layout = new QVBoxLayout(card);
            layout->setContentsMargins(18, 14, 18, 14);
            layout->setSpacing(8);
            auto title = new QLabel(names[i]);
            title->setObjectName("cardtitle");
            auto description = new QLabel(i < 2 ? "조이스틱 테두리 조명" : "기기 측면 조명");
            description->setObjectName("description");
            toggles[i] = new LedToggle("확인 중");
            toggles[i]->setCheckable(true);
            toggles[i]->setEnabled(false);
            effectButtons[i] = new QPushButton("효과: 계속 켜기");
            effectButtons[i]->setObjectName("effect");
            effectButtons[i]->setEnabled(false);
            layout->addWidget(title);
            layout->addWidget(description);
            layout->addWidget(toggles[i]);
            layout->addWidget(effectButtons[i]);
            grid->addWidget(card, i / 2, i % 2);
            connect(toggles[i], &QPushButton::clicked, this, [this, i] { change(ids[i], !states[i]); });
            connect(effectButtons[i], &QPushButton::clicked, this, [this, i] {
                if (!pending) request({"effect", ids[i], effectIds[(effects[i] + 1) % 4]});
            });
        }
        for (int row = 0; row < 2; ++row) {
            navigation.append(toggles[row * 2]);
            navigation.append(toggles[row * 2 + 1]);
            navigation.append(effectButtons[row * 2]);
            navigation.append(effectButtons[row * 2 + 1]);
        }
        body->addLayout(grid, 5);
        root->addLayout(body, 1);
        notice = new QLabel("조명을 확인하고 있습니다.");
        notice->setObjectName("subtitle");
        root->addWidget(notice);
        auto footer = new QHBoxLayout;
        allOn = new QPushButton("전체 켜기");
        allOff = new QPushButton("전체 끄기");
        allEffect = new QPushButton("전체 효과: 확인 중");
        auto back = new QPushButton("돌아가기");
        allOn->setEnabled(false);
        allOff->setEnabled(false);
        allEffect->setEnabled(false);
        connect(allOn, &QPushButton::clicked, this, [this] { change("all", true); });
        connect(allOff, &QPushButton::clicked, this, [this] { change("all", false); });
        connect(allEffect, &QPushButton::clicked, this, [this] {
            if (pending) return;
            const int current = commonEffect();
            request({"effect", "all", allEffectIds[current < 0 ? 0 : (current + 1) % (scanAvailable ? 5 : 4)]});
        });
        connect(back, &QPushButton::clicked, this, &QWidget::close);
        footer->addWidget(allOn);
        footer->addWidget(allOff);
        footer->addWidget(allEffect);
        footer->addStretch();
        footer->addWidget(back);
        root->addLayout(footer);
        navigation.append(allOn);
        navigation.append(allOff);
        navigation.append(allEffect);
        navigation.append(back);
        auto hints = new QLabel("방향키  이동     A / B  확인     Select + Start  종료  ·  터치로도 조작할 수 있습니다.");
        hints->setObjectName("description");
        root->addWidget(hints);
        qApp->installEventFilter(this);
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
        if (SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) == 0) {
            openController();
            connect(&controllerTimer, &QTimer::timeout, this, [this] { pollController(); });
            controllerTimer.start(16);
        }
        QTimer::singleShot(0, this, [this] { request({"status"}); });
    }

    ~LedWindow() override {
        if (controller) SDL_GameControllerClose(controller);
        SDL_Quit();
    }

    int commonEffect() const {
        if (groupEffect == "scan") return 4;
        for (int i = 1; i < 4; ++i)
            if (effects[i] != effects[0]) return -1;
        return effects[0];
    }

    void render() {
        bool complete = true;
        bool allBlinkAvailable = true;
        for (int i = 0; i < 4; ++i) {
            toggles[i]->setChecked(states[i]);
            toggles[i]->setText(available[i] ? (states[i] ? "켜짐" : "꺼짐") : "사용할 수 없음");
            toggles[i]->setEnabled(available[i]);
            effectButtons[i]->setText(groupRunning ? "효과: 전체 왕복 중" : "효과: " + effectNames[effects[i]]);
            effectButtons[i]->setEnabled(available[i] && blinkAvailable[i]);
            complete = complete && available[i];
            allBlinkAvailable = allBlinkAvailable && blinkAvailable[i];
        }
        allOn->setEnabled(complete);
        allOff->setEnabled(complete);
        const int common = commonEffect();
        allEffect->setText("전체 효과: " + (common < 0 ? QString("개별 설정") : allEffectNames[common]));
        allEffect->setEnabled(complete && allBlinkAvailable);
        diagram->states = states;
        diagram->update();
    }

    void acceptState(const QJsonObject &json) {
        pending = false;
        groupEffect = json.value("group_effect").toString("individual");
        groupRunning = json.value("group_running").toBool();
        scanAvailable = json.value("scan_available").toBool();
        const auto leds = json.value("leds").toObject();
        for (int i = 0; i < 4; ++i) {
            const auto led = leds.value(ids[i]).toObject();
            available[i] = led.value("available").toBool();
            states[i] = led.value("on").toBool();
            effects[i] = 0;
            for (int effect = 0; effect < 4; ++effect)
                if (led.value("effect").toString() == effectIds[effect]) effects[i] = effect;
            blinkAvailable[i] = led.value("effects").toArray().contains("slow");
        }
        notice->setText("켜기는 효과 없이 켜집니다. 효과는 효과 버튼에서 선택하세요. 자동 저장됩니다.");
        render();
        if (!loaded) {
            for (auto button : navigation)
                if (button->isEnabled()) { button->setFocus(); break; }
            loaded = true;
        }
        else if (!focusWidget() || !focusWidget()->isEnabled()) {
            for (auto button : navigation)
                if (button->isEnabled()) { button->setFocus(); break; }
        }
        if (closing) close();
    }

    void request(const QStringList &arguments) {
        if (pending) return;
        pending = true;
        render();
        if (preview) {
            QTimer::singleShot(previewDelayMs, this, [this, arguments] {
                if (arguments[0] == "set") {
                    const bool on = arguments[2] == "1";
                    if (on || (arguments[1] != "all" && groupEffect == "scan")) {
                        groupEffect = "individual";
                        groupRunning = false;
                    }
                    for (int i = 0; i < 4; ++i) {
                        if (arguments[1] != "all" && arguments[1] != ids[i]) continue;
                        states[i] = on;
                        if (on) effects[i] = 0;
                    }
                    if (groupEffect == "scan") groupRunning = on;
                    ++applied;
                }
                if (arguments[0] == "effect") {
                    groupEffect = arguments[2] == "scan" ? "scan" : "individual";
                    groupRunning = groupEffect == "scan";
                    if (groupRunning) effects.fill(0);
                    for (int i = 0; i < 4; ++i) {
                        if (arguments[1] != ids[i] && arguments[1] != "all") continue;
                        states[i] = true;
                        for (int effect = 0; effect < 4; ++effect)
                            if (arguments[2] == effectIds[effect]) effects[i] = effect;
                    }
                    ++applied;
                }
                QJsonObject leds;
                for (int i = 0; i < 4; ++i) leds[ids[i]] = QJsonObject{
                    {"available", true}, {"on", states[i]}, {"effect", effectIds[effects[i]]},
                    {"effects", QJsonArray{"steady", "slow", "normal", "fast"}}};
                acceptState(QJsonObject{{"leds", leds}, {"group_effect", groupEffect}, {"group_running", groupRunning}, {"scan_available", true}});
            });
            return;
        }
        process.disconnect();
        connect(&process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) failed();
        });
        connect(&process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                [this](int code, QProcess::ExitStatus exit) {
            const auto json = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
            if (code == 0 && exit == QProcess::NormalExit && json.contains("leds")) acceptState(json);
            else failed();
        });
        process.start(qEnvironmentVariable("ODIN_LEDCTL", "/usr/bin/odin-ledctl"), arguments);
    }

    void failed() {
        pending = false;
        notice->setText("설정을 적용하지 못했습니다. 다시 시도해 주세요.");
        render();
        if (closing) close();
    }

    void change(const QString &id, bool on) {
        if (pending) return;
        request({"set", id, on ? "1" : "0"});
    }

    void moveFocus(int difference) {
        int position = navigation.indexOf(qobject_cast<QPushButton *>(focusWidget()));
        if (position < 0) position = 0;
        int next = qBound(0, position + difference, int(navigation.size()) - 1);
        while (!navigation[next]->isEnabled() && next != position) {
            next += difference > 0 ? 1 : -1;
            if (next < 0 || next >= navigation.size()) return;
        }
        navigation[next]->setFocus();
    }

    void choose() {
        if (pending) return;
        if (auto button = qobject_cast<QPushButton *>(focusWidget())) button->click();
    }

    void closeEvent(QCloseEvent *event) override {
        if (pending) {
            closing = true;
            event->ignore();
        } else QWidget::closeEvent(event);
    }

    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() == QEvent::KeyPress) {
            auto key = static_cast<QKeyEvent *>(event)->key();
            switch (key) {
                case Qt::Key_Left: moveFocus(-1); return true;
                case Qt::Key_Right: moveFocus(1); return true;
                case Qt::Key_Up: moveFocus(-2); return true;
                case Qt::Key_Down: moveFocus(2); return true;
                case Qt::Key_X: case Qt::Key_Z:
                    if (!static_cast<QKeyEvent *>(event)->isAutoRepeat()) choose();
                    return true;
                case Qt::Key_Escape: close(); return true;
            }
        }
        return QWidget::eventFilter(object, event);
    }

    void openController() {
        if (controller) return;
        int selected = -1;
        for (int i = 0; i < SDL_NumJoysticks(); ++i) {
            if (!SDL_IsGameController(i)) continue;
            if (selected < 0) selected = i;
            if (SDL_JoystickGetDeviceVendor(i) == 0x054c) { selected = i; break; }
        }
        if (selected >= 0) controller = SDL_GameControllerOpen(selected);
    }

    void pollController() {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_CONTROLLERDEVICEADDED) openController();
            if (event.type == SDL_CONTROLLERDEVICEREMOVED && controller && !SDL_GameControllerGetAttached(controller)) {
                SDL_GameControllerClose(controller);
                controller = nullptr;
                selectHeld = startHeld = false;
                openController();
            }
            if (event.type == SDL_CONTROLLERBUTTONDOWN || event.type == SDL_CONTROLLERBUTTONUP) {
                if (controller && event.cbutton.which != SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller))) continue;
                const bool pressed = event.type == SDL_CONTROLLERBUTTONDOWN;
                if (event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) selectHeld = pressed;
                if (event.cbutton.button == SDL_CONTROLLER_BUTTON_START) startHeld = pressed;
                if (selectHeld && startHeld) { close(); continue; }
            }
            if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                switch (event.cbutton.button) {
                    case SDL_CONTROLLER_BUTTON_A: case SDL_CONTROLLER_BUTTON_B: choose(); break;
                    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: moveFocus(-1); break;
                    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: moveFocus(1); break;
                    case SDL_CONTROLLER_BUTTON_DPAD_UP: moveFocus(-2); break;
                    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: moveFocus(2); break;
                }
            }
            if (event.type == SDL_CONTROLLERAXISMOTION && event.caxis.axis <= SDL_CONTROLLER_AXIS_LEFTY) {
                if (controller && event.caxis.which != SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller))) continue;
                int direction = event.caxis.value < -18000 ? -1 : event.caxis.value > 18000 ? 1 : 0;
                int axis = event.caxis.axis;
                if (direction != 0 && axisDirections[axis] == 0) moveFocus(direction * (axis == 0 ? 1 : 2));
                axisDirections[axis] = direction;
            }
        }
    }
};

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    auto fontPath = qEnvironmentVariable("ODIN_GUI_FONT", "/usr/share/fonts/truetype/noto-cjk/NotoSansCJKsc-Regular.otf");
    QFontDatabase::addApplicationFont(fontPath);
    app.setApplicationName("odin-led-gui");
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"preview", "Preview without device writes."});
    parser.addOption({"self-test", "Exercise preview controls and gamepad events."});
    parser.addOption({"feedback-test", "Check stable button pixels during delayed commands."});
    parser.addOption({"capture", "Save a preview screenshot.", "path"});
    parser.addOption({"quit-after", "Close after the given number of milliseconds.", "milliseconds"});
    parser.process(app);
    const bool testing = parser.isSet("self-test");
    const bool feedbackTesting = parser.isSet("feedback-test");
    LedWindow window(parser.isSet("preview") || testing || feedbackTesting);
    if (feedbackTesting) window.previewDelayMs = 300;
    if (window.preview) window.show(); else window.showFullScreen();
    if (feedbackTesting) {
        auto images = std::make_shared<QList<QImage>>();
        auto rectangles = std::make_shared<QList<QRect>>();
        auto snapshot = [&window, images, rectangles] {
            images->clear();
            rectangles->clear();
            for (auto button : window.navigation) {
                images->append(button->grab().toImage());
                rectangles->append(button->geometry());
            }
        };
        auto stable = [&window, images, rectangles] {
            for (int i = 0; i < window.navigation.size(); ++i) {
                auto button = window.navigation[i];
                if (!button->isEnabled() || button->geometry() != rectangles->at(i)) std::exit(30);
                if (i && (button->isDown() || button->grab().toImage() != images->at(i))) std::exit(31);
            }
        };
        QTimer::singleShot(450, &window, [&window, snapshot] {
            if (!window.loaded || window.pending) std::exit(32);
            window.toggles[0]->setFocus();
            snapshot();
            window.toggles[0]->click();
        });
        QTimer::singleShot(530, &window, [&window, stable] {
            if (!window.pending || window.focusWidget() != window.toggles[0]) std::exit(33);
            stable();
            window.toggles[1]->click();
            window.allOn->click();
            window.allEffect->click();
            window.choose();
            window.request({"set", ids[1], "1"});
            if (window.toggles[0]->isChecked() || window.toggles[1]->isChecked()) std::exit(34);
            stable();
            std::puts("GUI_PENDING_OTHER_BUTTON_PIXELS_FOCUS_LAYOUT=PASS");
        });
        QTimer::singleShot(850, &window, [&window, stable, snapshot] {
            if (window.pending || window.applied != 1 || !window.states[0] || window.states[1]) std::exit(35);
            stable();
            if (!window.toggles[0]->isChecked()) std::exit(36);
            snapshot();
            const QPointF local = window.toggles[0]->rect().center();
            const QPointF global = window.toggles[0]->mapToGlobal(local.toPoint());
            QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease, local, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(window.toggles[0], &press);
            QApplication::sendEvent(window.toggles[0], &release);
        });
        QTimer::singleShot(930, &window, [&window, stable] {
            if (!window.pending || !window.toggles[0]->isChecked()) std::exit(37);
            stable();
            window.moveFocus(1);
        });
        QTimer::singleShot(1260, &window, [&window] {
            if (window.pending || window.applied != 2 || window.states != std::array<bool, 4>{}) std::exit(38);
            if (window.focusWidget() != window.toggles[1]) std::exit(39);
            std::puts("GUI_MOUSE_CONFIRMED_STATE_DUPLICATE_INPUT_AND_FOCUS=PASS");
            window.close();
        });
    }
    if (testing) {
        auto pushButton = [&window](Uint32 type, Uint8 button) {
            SDL_Event event{};
            event.type = type;
            event.cbutton.button = button;
            if (window.controller) event.cbutton.which = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(window.controller));
            SDL_PushEvent(&event);
        };
        QTimer::singleShot(100, &window, [&window] {
            if (window.focusWidget() != window.toggles[0]) std::exit(7);
            window.allOn->click();
        });
        QTimer::singleShot(200, &window, [&window] {
            if (window.states != std::array<bool, 4>{true, true, true, true}) std::exit(2);
            window.allOff->click();
        });
        QTimer::singleShot(300, &window, [&window] {
            if (window.states != std::array<bool, 4>{}) std::exit(3);
            window.toggles[0]->setFocus();
            SDL_Event event{};
            event.type = SDL_CONTROLLERBUTTONDOWN;
            if (window.controller) event.cbutton.which = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(window.controller));
            event.cbutton.button = SDL_CONTROLLER_BUTTON_A;
            SDL_PushEvent(&event);
        });
        QTimer::singleShot(450, &window, [&window] {
            if (!window.states[0] || window.states[1] || window.applied != 3) std::exit(4);
            window.toggles[1]->setFocus();
            SDL_Event event{};
            event.type = SDL_CONTROLLERBUTTONDOWN;
            event.cbutton.button = SDL_CONTROLLER_BUTTON_B;
            if (window.controller) event.cbutton.which = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(window.controller));
            SDL_PushEvent(&event);
        });
        QTimer::singleShot(600, &window, [&window] {
            if (!window.states[0] || !window.states[1] || window.applied != 4) std::exit(5);
            if (!window.isVisible()) std::exit(8);
            std::puts("GUI_CONTROLS_AND_GAMEPAD=PASS");
            window.effectButtons[0]->click();
        });
        QTimer::singleShot(640, &window, [&window] {
            if (window.effects[0] != 1 || !window.states[0] || window.applied != 5) std::exit(12);
            std::puts("GUI_LED_EFFECT_CONTROL=PASS");
            if (window.commonEffect() != -1 || !window.allEffect->text().contains("개별 설정")) std::exit(13);
            window.allEffect->click();
        });
        QTimer::singleShot(680, &window, [&window] {
            if (window.commonEffect() != 0 || window.states != std::array<bool, 4>{true, true, true, true}) std::exit(14);
            window.allEffect->click();
        });
        QTimer::singleShot(740, &window, [&window] {
            if (window.commonEffect() != 1) std::exit(15);
            window.allEffect->click();
        });
        QTimer::singleShot(810, &window, [&window] {
            if (window.commonEffect() != 2) std::exit(16);
            window.allEffect->click();
        });
        QTimer::singleShot(870, &window, [&window] {
            if (window.commonEffect() != 3) std::exit(17);
            window.allEffect->click();
        });
        QTimer::singleShot(910, &window, [&window] {
            if (window.commonEffect() != 4 || !window.groupRunning) std::exit(20);
            window.allOff->click();
        });
        QTimer::singleShot(960, &window, [&window] {
            if (window.states != std::array<bool, 4>{} || window.commonEffect() != 4 || window.groupRunning) std::exit(18);
            window.allOn->click();
        });
        QTimer::singleShot(1030, &window, [&window] {
            if (window.states != std::array<bool, 4>{true, true, true, true} || window.commonEffect() != 0 || window.groupRunning || window.applied != 12) std::exit(19);
            std::puts("GUI_ALL_EFFECT_CYCLE_AND_PLAIN_ON=PASS");
            window.effectButtons[0]->click();
        });
        QTimer::singleShot(1090, &window, [&window] {
            if (window.groupRunning || window.groupEffect == "scan" || window.effects[0] != 1) std::exit(21);
            window.allEffect->click();
        });
        QTimer::singleShot(1140, &window, [&window] {
            if (window.commonEffect() != 0 || window.groupRunning) std::exit(22);
            std::puts("GUI_SCAN_AND_MANUAL_EXIT=PASS");
        });
        QTimer::singleShot(650, &window, [pushButton] { pushButton(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_BACK); });
        QTimer::singleShot(720, &window, [&window, pushButton] {
            if (!window.isVisible() || !window.selectHeld || window.startHeld) std::exit(9);
            pushButton(SDL_CONTROLLERBUTTONUP, SDL_CONTROLLER_BUTTON_BACK);
        });
        QTimer::singleShot(770, &window, [pushButton] { pushButton(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_START); });
        QTimer::singleShot(840, &window, [&window, pushButton] {
            if (!window.isVisible() || window.selectHeld || !window.startHeld) std::exit(10);
            pushButton(SDL_CONTROLLERBUTTONUP, SDL_CONTROLLER_BUTTON_START);
        });
        QTimer::singleShot(1250, &window, [pushButton] { pushButton(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_START); });
        QTimer::singleShot(1320, &window, [pushButton] { pushButton(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_BACK); });
    }
    if (parser.isSet("capture")) {
        QTimer::singleShot(1180, &window, [&window, &parser] {
            if (!window.grab().save(parser.value("capture"))) std::exit(6);
            std::puts("GUI_SCREENSHOT=PASS");
        });
    }
    if (parser.isSet("quit-after"))
        QTimer::singleShot(parser.value("quit-after").toInt(), &app, &QApplication::quit);
    const int result = app.exec();
    if (testing) {
        if (window.isVisible() || !window.selectHeld || !window.startHeld) return 11;
        std::puts("GUI_AB_CONFIRM_AND_SELECT_START_EXIT=PASS");
    }
    return result;
}
