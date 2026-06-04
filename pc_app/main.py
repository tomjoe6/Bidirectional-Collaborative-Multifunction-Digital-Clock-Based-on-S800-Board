"""Entry point for the S800 Smart Clock Digital Twin PC application.

Initializes the PyQt5 application, loads environment configuration
via python-dotenv, and launches the main window.
"""

import sys
import os

from dotenv import load_dotenv
from PyQt5.QtWidgets import QApplication

from .config import APP_NAME


def main() -> int:
    """Run the S800 digital twin application.

    Returns:
        Exit code (0 on success).
    """
    # Load .env file for default configuration
    # Look for .env in the current working directory and the package directory
    env_paths = [
        os.path.join(os.getcwd(), ".env"),
        os.path.join(os.path.dirname(__file__), ".env"),
    ]
    for env_path in env_paths:
        if os.path.isfile(env_path):
            load_dotenv(env_path)
            break
    else:
        # Try default load_dotenv behavior
        load_dotenv()

    # Create Qt application
    app = QApplication(sys.argv)
    app.setApplicationName(APP_NAME)
    app.setOrganizationName("S800")
    app.setOrganizationDomain("s800.local")

    # Apply a dark fusion-like stylesheet for the whole app
    app.setStyle("Fusion")
    _apply_dark_theme(app)

    # Import here to avoid circular imports at module level
    from .main_window import MainWindow

    window = MainWindow()
    window.show()

    return app.exec_()


def _apply_dark_theme(app: QApplication) -> None:
    """Apply a dark color theme to the application."""
    dark_palette = """
    QMainWindow { background-color: #2B2B2B; }
    QWidget { color: #D4D4D4; background-color: #2B2B2B; }
    QGroupBox {
        border: 1px solid #555555;
        border-radius: 4px;
        margin-top: 10px;
        padding-top: 10px;
        font-weight: bold;
        color: #E0E0E0;
    }
    QGroupBox::title {
        subcontrol-origin: margin;
        left: 10px;
        padding: 0 5px;
    }
    QPushButton {
        background-color: #3C3C3C;
        border: 1px solid #555555;
        border-radius: 3px;
        padding: 4px 10px;
        color: #D4D4D4;
    }
    QPushButton:hover { background-color: #4A4A4A; }
    QPushButton:pressed { background-color: #2A2A2A; }
    QPushButton:disabled { color: #666666; }
    QComboBox {
        background-color: #3C3C3C;
        border: 1px solid #555555;
        border-radius: 3px;
        padding: 2px 8px;
        color: #D4D4D4;
    }
    QComboBox QAbstractItemView {
        background-color: #3C3C3C;
        color: #D4D4D4;
        selection-background-color: #4A4A4A;
    }
    QSpinBox {
        background-color: #3C3C3C;
        border: 1px solid #555555;
        border-radius: 3px;
        padding: 2px 4px;
        color: #D4D4D4;
    }
    QLineEdit {
        background-color: #3C3C3C;
        border: 1px solid #555555;
        border-radius: 3px;
        padding: 2px 6px;
        color: #D4D4D4;
    }
    QLabel { background-color: transparent; }
    QSplitter::handle { background-color: #555555; width: 1px; }
    QScrollBar:vertical {
        background-color: #2B2B2B;
        width: 10px;
        border: none;
    }
    QScrollBar::handle:vertical {
        background-color: #555555;
        border-radius: 5px;
        min-height: 20px;
    }
    QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
    QToolTip {
        background-color: #3C3C3C;
        color: #D4D4D4;
        border: 1px solid #555555;
    }
    """
    app.setStyleSheet(dark_palette)


if __name__ == "__main__":
    sys.exit(main())
