"""Holiday greeting — auto-display on boot for major Chinese holidays."""
from datetime import date

HOLIDAYS = {
    (1,  1):  "HappyNew",
    (2, 14):  "Love You",
    (5,  1):  "51Happy ",
    (6,  1):  "61Happy ",
    (9, 10):  "Teachers",
    (10, 1):  "NationDy",
    (10, 2):  "NationDy",
    (10, 3):  "NationDy",
    (12, 25): "MerryXma",
}

def get_greeting() -> str:
    """Return a ≤8-char holiday greeting, or empty string."""
    today = date.today()
    return HOLIDAYS.get((today.month, today.day), "")
