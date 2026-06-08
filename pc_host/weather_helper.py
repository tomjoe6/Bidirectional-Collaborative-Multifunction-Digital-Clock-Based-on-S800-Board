"""Weather data client for the S800 clock system (Extension E2).

Fetches real-time weather summary from a configured API (OpenWeatherMap
or Hefeng), caches the result with a configurable TTL, and formats a
compact ≤8-character ASCII string suitable for the 7-segment display.
"""

from __future__ import annotations

import os
import time
from dataclasses import dataclass, field
from typing import Optional

import requests

from config import (
    WEATHER_DEFAULT_CACHE_TTL,
    WEATHER_DEFAULT_LOCATION,
)


# Mapping from OpenWeatherMap weather main codes to ≤4-char abbreviations
_OWM_WEATHER_ABBREV: dict = {
    "Clear":        "Sunny",
    "Clouds":       "Cldy",
    "Rain":         "Rain",
    "Drizzle":      "Drzl",
    "Thunderstorm": "Thdr",
    "Snow":         "Snow",
    "Mist":         "Mist",
    "Haze":         "Haze",
    "Fog":          "Fog",
    "Dust":         "Dust",
    "Sand":         "Sand",
    "Smoke":        "Smok",
    "Tornado":      "Trnd",
    "Squall":       "Sqll",
}


@dataclass
class WeatherResult:
    """Result of a weather data fetch.

    Attributes:
        success: True if weather data was successfully fetched.
        display_text: ≤8 ASCII characters for 7-segment display, valid
                      only if success (e.g. "Sunny26C").
        error_msg: Human-readable error description if not success.
        timestamp: Unix timestamp when this result was fetched.
    """

    success: bool
    display_text: str = ""
    error_msg: str = ""
    timestamp: float = 0.0


@dataclass
class WeatherCache:
    """In-memory cache for weather data with TTL expiry.

    Attributes:
        result: The cached WeatherResult, or None if never fetched.
        ttl_seconds: Cache time-to-live in seconds.
    """

    result: Optional[WeatherResult] = None
    ttl_seconds: int = WEATHER_DEFAULT_CACHE_TTL

    @property
    def is_valid(self) -> bool:
        """Return True if the cache has a valid, non-expired result."""
        if self.result is None or not self.result.success:
            return False
        elapsed = time.time() - self.result.timestamp
        return elapsed < self.ttl_seconds

    @property
    def age_seconds(self) -> float:
        """Return the age of the cached result in seconds, or -1 if empty."""
        if self.result is None:
            return -1.0
        return time.time() - self.result.timestamp

    def age_text(self) -> str:
        """Return a human-readable age string (e.g. '12分钟前')."""
        age = self.age_seconds
        if age < 0:
            return "无缓存"
        if age < 60:
            return f"{int(age)}秒前"
        if age < 3600:
            return f"{int(age / 60)}分钟前"
        return f"{int(age / 3600)}小时前"


class WeatherClient:
    """Client for fetching weather data from a configured API.

    Supports OpenWeatherMap (default) and Hefeng (和风天气) APIs.
    Configuration is read from environment variables; see .env.example
    for available options.
    """

    # OpenWeatherMap current weather endpoint
    _OWM_URL = "https://api.openweathermap.org/data/2.5/weather"

    # Hefeng (和风天气) v7 now-weather endpoint
    _HEFENG_URL = "https://devapi.qweather.com/v7/weather/now"

    def __init__(self) -> None:
        """Initialize the weather client with env-var configuration."""
        self._api_key: str = os.getenv("WEATHER_API_KEY", "")
        self._location: str = os.getenv("WEATHER_LOCATION", WEATHER_DEFAULT_LOCATION)
        self._api_type: str = os.getenv("WEATHER_API_TYPE", "openweathermap").lower()
        ttl_str = os.getenv("WEATHER_CACHE_TTL", "")
        self._cache_ttl: int = WEATHER_DEFAULT_CACHE_TTL
        if ttl_str:
            try:
                self._cache_ttl = int(ttl_str)
            except ValueError:
                pass
        self._cache = WeatherCache(ttl_seconds=self._cache_ttl)

    @property
    def has_api_key(self) -> bool:
        """Return True if an API key is configured."""
        return len(self._api_key.strip()) > 0

    @property
    def cache(self) -> WeatherCache:
        """Return the weather cache object."""
        return self._cache

    def fetch(self) -> WeatherResult:
        """Fetch current weather data from the configured API.

        Returns:
            WeatherResult with success=True and display_text on success,
            or success=False with error_msg on failure.
        """
        if not self.has_api_key:
            return WeatherResult(
                success=False,
                error_msg="未配置 WEATHER_API_KEY，请在 .env 中设置",
            )

        if self._api_type == "hefeng":
            result = self._fetch_hefeng()
        else:
            result = self._fetch_openweathermap()

        if result.success:
            self._cache.result = result

        return result

    def get_display_text(self) -> str:
        """Return cached weather display text, or 'NO DATA' if unavailable.

        Returns:
            ≤8 ASCII characters for 7-segment display.
        """
        if self._cache.is_valid and self._cache.result is not None:
            return self._cache.result.display_text
        return "NO DATA"

    # ------------------------------------------------------------------
    # OpenWeatherMap API
    # ------------------------------------------------------------------

    def _fetch_openweathermap(self) -> WeatherResult:
        """Fetch weather from OpenWeatherMap Current Weather API."""
        now = time.time()
        try:
            resp = requests.get(
                self._OWM_URL,
                params={
                    "q": self._location,
                    "appid": self._api_key,
                    "units": "metric",
                },
                timeout=5,
            )
            resp.raise_for_status()
            data = resp.json()
        except requests.exceptions.Timeout:
            return WeatherResult(
                success=False,
                error_msg="天气API请求超时 (5s)",
            )
        except requests.exceptions.ConnectionError:
            return WeatherResult(
                success=False,
                error_msg="无法连接天气API服务器",
            )
        except requests.exceptions.HTTPError as exc:
            if resp is not None and resp.status_code == 401:
                return WeatherResult(
                    success=False,
                    error_msg="天气API Key无效 (401)",
                )
            return WeatherResult(
                success=False,
                error_msg=f"天气API HTTP错误: {exc}",
            )
        except requests.exceptions.RequestException as exc:
            return WeatherResult(
                success=False,
                error_msg=f"天气API请求失败: {exc}",
            )
        except ValueError:
            return WeatherResult(
                success=False,
                error_msg="天气API返回数据格式异常",
            )

        try:
            weather_main = data["weather"][0]["main"]
            temp_c = int(round(data["main"]["temp"]))
        except (KeyError, IndexError, TypeError):
            return WeatherResult(
                success=False,
                error_msg="天气API响应缺少预期字段",
            )

        abbrev = _OWM_WEATHER_ABBREV.get(weather_main, weather_main[:4])
        display_text = f"{abbrev}{temp_c}C"

        # Ensure ≤8 characters
        if len(display_text) > 8:
            display_text = display_text[:8]

        return WeatherResult(
            success=True,
            display_text=display_text,
            timestamp=now,
        )

    # ------------------------------------------------------------------
    # Hefeng (和风天气) API
    # ------------------------------------------------------------------

    def _fetch_hefeng(self) -> WeatherResult:
        """Fetch weather from Hefeng (和风天气) v7 API."""
        now = time.time()
        try:
            resp = requests.get(
                f"{self._HEFENG_URL}/{self._location}",
                params={"key": self._api_key},
                timeout=5,
            )
            resp.raise_for_status()
            data = resp.json()
        except requests.exceptions.Timeout:
            return WeatherResult(
                success=False,
                error_msg="天气API请求超时 (5s)",
            )
        except requests.exceptions.ConnectionError:
            return WeatherResult(
                success=False,
                error_msg="无法连接天气API服务器",
            )
        except requests.exceptions.HTTPError as exc:
            if resp is not None and resp.status_code in (401, 403):
                return WeatherResult(
                    success=False,
                    error_msg="天气API Key无效",
                )
            return WeatherResult(
                success=False,
                error_msg=f"天气API HTTP错误: {exc}",
            )
        except requests.exceptions.RequestException as exc:
            return WeatherResult(
                success=False,
                error_msg=f"天气API请求失败: {exc}",
            )
        except ValueError:
            return WeatherResult(
                success=False,
                error_msg="天气API返回数据格式异常",
            )

        # Hefeng response: {"now": {"text": "晴", "temp": "26", ...}, "code": "200"}
        try:
            code = data.get("code", "")
            if code != "200":
                return WeatherResult(
                    success=False,
                    error_msg=f"和风天气API返回错误码: {code}",
                )
            now_data = data["now"]
            weather_text = now_data["text"]  # e.g. "晴", "多云"
            temp_c = int(now_data["temp"])
        except (KeyError, TypeError, ValueError):
            return WeatherResult(
                success=False,
                error_msg="和风天气API响应缺少预期字段",
            )

        # Combine weather text + temperature, keeping ≤8 chars
        display_text = f"{weather_text}{temp_c}C"
        if len(display_text) > 8:
            # Truncate weather text
            max_weather_len = 8 - len(f"{temp_c}C")
            if max_weather_len > 0:
                display_text = f"{weather_text[:max_weather_len]}{temp_c}C"
            else:
                display_text = f"{temp_c}C"[:8]

        return WeatherResult(
            success=True,
            display_text=display_text,
            timestamp=now,
        )
