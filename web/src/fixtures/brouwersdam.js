const sampleTimes = ['08', '11', '14', '17', '20']

function samples(values) {
  return values.map(([sustainedKt, gustKt, destinationDegrees], index) => ({
    time: sampleTimes[index],
    sustainedKt,
    gustKt,
    destinationDegrees,
    available: true,
    weather: 1,
    temperatureTenthsC: 120 + index * 5,
    temperatureAvailable: true,
  }))
}

export const brouwersdamForecast = Object.freeze({
  schemaVersion: 3,
  spotId: 'brouwersdam',
  spotName: 'Brouwersdam',
  timezone: 'Europe/Amsterdam',
  deviceTimezone: 'Europe/Amsterdam',
  provider: 'OPEN-METEO',
  modelId: 'best_match',
  model: 'BEST MATCH',
  updatedTime: '07:40',
  retrievedAt: 1787722800000,
  days: [
    { localDate: '2026-08-26', day: 'WED', date: '26 AUG', samples: samples([[8, 13, 250], [12, 18, 258], [16, 24, 264], [19, 28, 270], [15, 22, 274]]) },
    { localDate: '2026-08-27', day: 'THU', date: '27 AUG', samples: samples([[13, 19, 268], [17, 25, 272], [21, 30, 278], [23, 34, 282], [18, 27, 286]]) },
    { localDate: '2026-08-28', day: 'FRI', date: '28 AUG', samples: samples([[15, 22, 282], [19, 28, 286], [24, 35, 290], [26, 38, 294], [20, 30, 298]]) },
    { localDate: '2026-08-29', day: 'SAT', date: '29 AUG', samples: samples([[10, 16, 236], [13, 20, 242], [17, 26, 248], [18, 27, 252], [14, 21, 258]]) },
    { localDate: '2026-08-30', day: 'SUN', date: '30 AUG', samples: samples([[7, 11, 198], [10, 15, 204], [12, 18, 210], [14, 21, 214], [11, 17, 220]]) },
  ],
})

export const brouwersdamTide = Object.freeze({
  capability: 'available',
  spotId: 'brouwersdam',
  timezone: 'Europe/Amsterdam',
  samples: brouwersdamForecast.days.flatMap((day, dayIndex) =>
    Array.from({ length: 24 }, (_, hour) => ({
      localDate: day.localDate,
      localTime: `${String(hour).padStart(2, '0')}:00`,
      seaLevelMm: Math.round(Math.sin((dayIndex * 24 + hour - 2) * Math.PI / 6.2) * 900),
    }))),
  extrema: brouwersdamForecast.days.flatMap((day, dayIndex) => [
    { localDate: day.localDate, localTime: dayIndex % 2 ? '03:15' : '02:45', seaLevelMm: 900, type: 'high' },
    { localDate: day.localDate, localTime: dayIndex % 2 ? '09:30' : '09:00', seaLevelMm: -900, type: 'low' },
    { localDate: day.localDate, localTime: dayIndex % 2 ? '15:45' : '15:15', seaLevelMm: 900, type: 'high' },
    { localDate: day.localDate, localTime: dayIndex % 2 ? '22:00' : '21:30', seaLevelMm: -900, type: 'low' },
  ]),
})
