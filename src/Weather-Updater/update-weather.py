from datetime import datetime
import json
import urllib.request
import time
from weather_api_key import *


def main():
    nowStr = humanStrFromTimestamp(datetime.now())

    weather = ""
    with open('weather.txt','r',encoding='utf8') as weatherFile:
        weather = parseWeather(weatherFile.read())

    if True or len(weather) == 0 or nowStr.endswith('0'):
        weather = fetchWeather()

    with open('weather.txt','w',encoding='utf8') as weatherFile:
        weatherFile.write(f"{nowStr}|{weather}")

def humanStrFromTimestamp(ts):
    if type(ts) is int:
        ts = datetime.fromtimestamp(ts)
    result = ":".join(ts.strftime("%X").split(":")[0:-1])
    if result.startswith("0"):
        return result[1:]
    else:
        return result


def parseWeather(currentFile):
    parts = currentFile.split('|')
    if len(parts) > 1:
        return "|".join(currentFile.split('|')[1:])
    else:
        return ""


def fetchWeather():
    url = "https://api.openweathermap.org/data/3.0/onecall?lat=40.4846938&lon=-79.9370109&units=imperial&appid="
    url += weather_api_key

    weatherJson = urllib.request.urlopen(url).read()
    weather = json.loads(weatherJson)
    # the first entry [0] appears to be in the recent past / current.
    tempIn4h = float(weather["hourly"][4]["temp"])
    precipIn4h = float(weather["hourly"][4]["pop"]) * 100
    result = f"In 4h: {tempIn4h:.0f}^ {precipIn4h:.0f}%"
    tempIn8h = float(weather["hourly"][8]["temp"])
    precipIn8h = float(weather["hourly"][8]["pop"]) * 100
    result += f"|In 8h: {tempIn8h:.0f}^ {precipIn8h:.0f}%"
    tempIn24h = float(weather["hourly"][24]["temp"])
    precipIn24h = float(weather["hourly"][24]["pop"]) * 100
    result += f"|In 24h: {tempIn24h:.0f}^ {precipIn24h:.0f}%"
    sunriseTime = int(weather["current"]["sunrise"])
    sunsetTime = int(weather["current"]["sunset"])
    sunriseTimeStr = humanStrFromTimestamp(sunriseTime)
    result += f"|Sunrise: {sunriseTimeStr}"
    sunsetTimeStr = humanStrFromTimestamp(sunsetTime)
    result += f"|Sunset: {sunsetTimeStr}"
    return result


if __name__ == "__main__":
    main()
