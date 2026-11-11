"""Compact host controls; guests retain their own volume and cache status."""
import sys


STYLE = '''
QWidget { background:#1e1e1e; color:#ececec; font:13px "Noto Sans"; }
QLabel { background:transparent; }
QLabel#title { font-size:18px; font-weight:600; }
QLabel#muted { color:#a6a6a6; }
QLabel#feedback { color:#dfba7c; border-top:1px solid #333; padding-top:10px; }
QLineEdit,QPushButton,QDoubleSpinBox { background:#252525; border:1px solid #444; border-radius:3px; padding:7px; min-height:20px; }
QPushButton:hover { background:#303030; }
QPushButton:disabled { color:#777; }
QLineEdit:focus,QPushButton:focus,QDoubleSpinBox:focus { border-color:#b4b4b4; }
QPushButton#play { background:#ececec; color:#202020; }
QPushButton#play:disabled { background:#393939; color:#929292; }
QSlider::groove:horizontal { background:#353535; height:4px; }
QSlider::handle:horizontal { background:#ccc; width:12px; margin:-5px 0; border-radius:3px; }
'''


def make_panel(bridge, worker):
    from PySide6.QtCore import Qt, QTimer
    from PySide6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QLabel, QLineEdit,
                                 QPushButton, QDoubleSpinBox, QSlider)

    class Panel(QWidget):
        def __init__(self):
            super().__init__()
            self.setWindowTitle('Moon Sweepers · YouTube Music')
            self.setMinimumWidth(400); self.resize(640, 340)
            self.setStyleSheet(STYLE)
            layout = QVBoxLayout(self); layout.setContentsMargins(18, 16, 18, 16); layout.setSpacing(12)
            header = QHBoxLayout(); layout.addLayout(header)
            title = QLabel('YouTube Music'); title.setObjectName('title'); header.addWidget(title)
            header.addStretch(); self.role = QLabel('Waiting for a world'); self.role.setObjectName('muted'); header.addWidget(self.role)
            self.url = QLineEdit(); self.url.setPlaceholderText('Paste a YouTube video link'); self.url.setAccessibleName('YouTube video link')
            layout.addWidget(self.url)
            controls = QHBoxLayout(); layout.addLayout(controls)
            self.play = QPushButton('Play'); self.play.setObjectName('play'); controls.addWidget(self.play)
            self.pause = QPushButton('Pause'); controls.addWidget(self.pause)
            self.resume = QPushButton('Resume'); controls.addWidget(self.resume)
            self.stop = QPushButton('Stop'); controls.addWidget(self.stop); controls.addStretch()
            self.retry = QPushButton('Retry'); controls.addWidget(self.retry)
            self.retry.clicked.connect(worker.retry)
            self.play.clicked.connect(self.select); self.url.returnPressed.connect(self.select)
            for button, action in ((self.pause, 'pause'), (self.resume, 'resume'), (self.stop, 'stop')):
                button.clicked.connect(lambda checked=False, action=action: self.command(action))
            self.now = QLabel('Load a world with the mod installed.'); self.now.setWordWrap(True); layout.addWidget(self.now)
            row = QHBoxLayout(); layout.addLayout(row)
            row.addWidget(QLabel('Position'))
            self.seek = QDoubleSpinBox(); self.seek.setSuffix(' s'); self.seek.setRange(0, 600); self.seek.setDecimals(1)
            self.seek.setAccessibleName('Seek position in seconds'); row.addWidget(self.seek)
            self.seek_button = QPushButton('Seek'); row.addWidget(self.seek_button); row.addStretch()
            self.seek_button.clicked.connect(lambda: self.command('seek', int(self.seek.value() * 1000)))
            row = QHBoxLayout(); layout.addLayout(row)
            row.addWidget(QLabel('My volume'))
            self.volume = QSlider(Qt.Orientation.Horizontal); self.volume.setRange(0, 100); self.volume.setValue(70)
            self.volume.setAccessibleName('My music volume'); row.addWidget(self.volume, 1)
            self.volume_value = QLabel('70%'); self.volume_value.setMinimumWidth(40); row.addWidget(self.volume_value)
            self.volume.valueChanged.connect(lambda n: self.volume_value.setText(str(n) + '%'))
            self.volume.sliderReleased.connect(lambda: self.command('volume', self.volume.value()))
            # Arrow-key changes commit on key release as well as pointer drags.
            original = self.volume.keyReleaseEvent
            def key_release(event):
                original(event); self.command('volume', self.volume.value())
            self.volume.keyReleaseEvent = key_release
            layout.addStretch()
            self.feedback = QLabel(''); self.feedback.setObjectName('feedback'); self.feedback.setWordWrap(True); layout.addWidget(self.feedback)
            self.hint = QLabel('The host selects tracks. Every player runs this helper. Up to 10 minutes per track.')
            self.hint.setObjectName('muted'); self.hint.setWordWrap(True); layout.addWidget(self.hint)
            self.timer = QTimer(self); self.timer.timeout.connect(self.refresh); self.timer.start(300)
            self.message = ''; self.message_until = 0
            self.refresh()

        def error(self, message):
            import time
            self.message = str(message); self.message_until = time.monotonic() + 6

        def command(self, action, value=0):
            try:
                if action != 'volume':
                    worker.cancel_selection()
                bridge.send(action, value)
            except (OSError, ValueError) as error:
                self.error(error)

        def select(self):
            try:
                worker.play(self.url.text())
            except (OSError, ValueError) as error:
                self.error(error)

        def refresh(self):
            import time
            state = bridge.state()
            active = bool(state and state['active'])
            host = bool(active and state['host'])
            for widget in (self.url, self.play, self.pause, self.resume, self.stop, self.seek, self.seek_button):
                widget.setEnabled(host)
            self.volume.setEnabled(active)
            if state:
                self.role.setText('Host' if state['host'] else 'Guest' if active else 'Waiting for host')
                position, duration = state['position_ms'] / 1000, state['duration_ms'] / 1000
                text = state['video'] or 'No track selected'
                self.now.setText(f'{text}  ·  {position:.1f} / {duration:.1f} s  ·  {state["status"].replace("-", " ")}')
                self.seek.setMaximum(duration or 600)
            else:
                self.role.setText('Waiting for a world'); self.now.setText('Load a world with the mod installed.')
            self.feedback.setText(self.message if time.monotonic() < self.message_until else worker.message or 'Ready')

    return Panel()


def show(bridge, worker):
    from PySide6.QtWidgets import QApplication
    app = QApplication.instance() or QApplication(sys.argv[:1])
    panel = make_panel(bridge, worker); panel.show()
    return app.exec()
