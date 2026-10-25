#!/usr/bin/env python3
"""Краткая инструкция «Быстрый старт» (PDF, A4) для пользователей: контроллер, веб-интерфейс, приложение.

  python3 tools/make_quickstart_pdf.py [docs/QUICKSTART.pdf]

Нужны reportlab и шрифт DejaVu Sans (с кириллицей). Текст - в этом файле: при изменении функций проверь инструкцию.
"""
import sys
from reportlab.lib import colors
from reportlab.lib.enums import TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.units import mm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import BaseDocTemplate, Frame, KeepTogether, PageTemplate, Paragraph, Spacer, Table, TableStyle

FONT_DIR = "/usr/share/fonts/truetype/dejavu/"
pdfmetrics.registerFont(TTFont("DV", FONT_DIR + "DejaVuSans.ttf"))
pdfmetrics.registerFont(TTFont("DV-B", FONT_DIR + "DejaVuSans-Bold.ttf"))
pdfmetrics.registerFont(TTFont("DV-M", FONT_DIR + "DejaVuSansMono.ttf"))
pdfmetrics.registerFontFamily("DV", normal="DV", bold="DV-B", italic="DV", boldItalic="DV-B")

INK = colors.HexColor("#1B2030")
MUTED = colors.HexColor("#5B647C")
ACCENT = colors.HexColor("#E08F00")
LINE = colors.HexColor("#D8DEEA")
SOFT = colors.HexColor("#F2F4F9")
WARN = colors.HexColor("#FFF4DB")

base = ParagraphStyle("base", fontName="DV", fontSize=9, leading=12.4, textColor=INK, alignment=TA_LEFT)
h1 = ParagraphStyle("h1", parent=base, fontName="DV-B", fontSize=21, leading=25, textColor=INK)
sub = ParagraphStyle("sub", parent=base, fontSize=9.5, leading=13, textColor=MUTED)
h2 = ParagraphStyle("h2", parent=base, fontName="DV-B", fontSize=12, leading=15, textColor=INK, spaceBefore=9, spaceAfter=3)
cell = ParagraphStyle("cell", parent=base, fontSize=8.6, leading=11.4)
cellb = ParagraphStyle("cellb", parent=cell, fontName="DV-B")
code = ParagraphStyle("code", parent=base, fontName="DV-M", fontSize=8.4, leading=11, backColor=SOFT, borderPadding=(3, 4, 3, 4), spaceBefore=2, spaceAfter=4)
note = ParagraphStyle("note", parent=base, fontSize=8.6, leading=11.6, backColor=WARN, borderPadding=(4, 6, 4, 6), spaceBefore=4, spaceAfter=4)
bullet = ParagraphStyle("bullet", parent=base, leftIndent=11, bulletIndent=0, spaceAfter=1.5)
step = ParagraphStyle("step", parent=base, leftIndent=14, bulletIndent=0, spaceAfter=1.8)


def P(t, s=base):
    return Paragraph(t, s)


def bl(items):
    return [Paragraph(t, bullet, bulletText="•") for t in items]


def steps(items):
    return [Paragraph(t, step, bulletText=f"{i}.") for i, t in enumerate(items, 1)]


def table(rows, widths, head=True):
    data = []
    for r, row in enumerate(rows):
        data.append([Paragraph(c, cellb if (head and r == 0) else cell) for c in row])
    t = Table(data, colWidths=[w * mm for w in widths], repeatRows=1 if head else 0)
    st = [
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("GRID", (0, 0), (-1, -1), 0.4, LINE),
        ("LEFTPADDING", (0, 0), (-1, -1), 4), ("RIGHTPADDING", (0, 0), (-1, -1), 4),
        ("TOPPADDING", (0, 0), (-1, -1), 2.5), ("BOTTOMPADDING", (0, 0), (-1, -1), 2.5),
    ]
    if head:
        st += [("BACKGROUND", (0, 0), (-1, 0), SOFT)]
    t.setStyle(TableStyle(st))
    return t


def footer(canvas, doc):
    canvas.saveState()
    canvas.setFont("DV", 7.5)
    canvas.setFillColor(MUTED)
    canvas.drawString(15 * mm, 8 * mm, "GyverTwink UK_Mode · быстрый старт · github.com/UkWood/GyverTwink (README и docs/ — подробности)")
    canvas.drawRightString(A4[0] - 15 * mm, 8 * mm, f"стр. {doc.page}")
    canvas.setStrokeColor(ACCENT)
    canvas.setLineWidth(1.2)
    canvas.line(15 * mm, A4[1] - 10 * mm, A4[0] - 15 * mm, A4[1] - 10 * mm)
    canvas.restoreState()


def build(out):
    doc = BaseDocTemplate(out, pagesize=A4, leftMargin=15 * mm, rightMargin=15 * mm, topMargin=14 * mm, bottomMargin=14 * mm,
                          title="GyverTwink UK_Mode: быстрый старт", author="GyverTwink UK_Mode", subject="Краткая инструкция: контроллер, веб-интерфейс, приложение")
    frame = Frame(doc.leftMargin, doc.bottomMargin, doc.width, doc.height, id="f", leftPadding=0, rightPadding=0, topPadding=0, bottomPadding=0)
    doc.addPageTemplates([PageTemplate(id="p", frames=[frame], onPage=footer)])
    s = []

    s += [P("GyverTwink UK_Mode — быстрый старт", h1),
          P("Гирлянда на адресных светодиодах (WS2812) и ESP8266 с управлением по WiFi. Умеет запоминать положение каждого светодиода, "
            "поэтому эффекты идут по форме гирлянды, а не по номеру в ленте. Нужна прошивка <b>1.4+</b>, приложение <b>2.0.0</b>.", sub),
          Spacer(1, 4)]

    s += [P("Из чего состоит комплекс", h2),
          table([
              ["Часть", "Что делает"],
              ["<b>Контроллер</b> (ESP8266 + лента)", "Хранит эффекты, сцены, расписание и координаты светодиодов. <b>Веб-страница управления хранится в нём самом</b>, интернет не нужен."],
              ["<b>Веб-интерфейс</b>", "Весь функционал в браузере телефона или компьютера: <font name='DV-M'>http://gyvertwink.local</font> или IP гирлянды."],
              ["<b>Приложение для Android</b> 2.0.0", "Оболочка вокруг веб-интерфейса: ищет гирлянды в сети, открывает ту же страницу, выбирает файлы, снимает калибровку камерой телефона."],
          ], [48, 132])]

    s += [P("1. Подключение и питание", h2)]
    s += bl([
        "Вход ленты (Din) — на <b>D1</b>, кнопка — между <b>D3</b> и <b>GND</b>. Схемы: папка <font name='DV-M'>schemes/</font> (рекомендуется: резистор 220 Ом в линии данных, конденсатор 100 мкФ по питанию). Ленты 12 В (WS2811): плату питать отдельно от USB.",
        "<b>Питание:</b> светодиод берёт до 60 мА, то есть 100 шт. — до 6 А, 500 — до 30 А, 1000 (предел) — до 60 А. Не хватает блока питания — задайте <b>лимит тока</b> (вкладка «Управление», около 80% от тока блока); гирлянда сама снизит яркость.",
    ])

    s += [P("2. Прошивка контроллера", h2)]
    s += steps([
        "Скачайте файл со страницы Releases репозитория: <font name='DV-M'>firmware-d1_mini.bin</font> (WeMos D1 mini) или <font name='DV-M'>firmware-nodemcuv2.bin</font> (NodeMCU).",
        "Первый раз — <b>по USB</b>, адрес <font name='DV-M'>0x0</font>. Из браузера (Chrome или Edge): <font name='DV-M'>adafruit.github.io/Adafruit_WebSerial_ESPTool</font> → Connect → Offset <font name='DV-M'>0x0</font> → выбрать файл → Program. Или из командной строки:",
    ])
    s += [P("pip install esptool<br/>esptool.py --port COM3 write_flash 0x0 firmware-d1_mini.bin", code),
          P("Плату, на которой что-то было, сначала очистите: <font name='DV-M'>esptool.py --port COM3 erase_flash</font> (сотрутся старые настройки WiFi). Из исходников: <font name='DV-M'>pio run -e d1_mini -t upload</font>. "
            "Число светодиодов и пины меняются в начале <font name='DV-M'>GyverTwink_v1.2.ino</font> (по умолчанию D1/D3). "
            "Дальше обновлять можно <b>по WiFi</b>: нажмите кнопку на гирлянде <b>три раза подряд</b> (лента мигнёт голубым, окно открыто 5 минут), "
            "в веб-интерфейсе вкладка «Прошивка» → выберите файл → «Загрузить». Не выключайте питание во время записи.", base)]

    s += [P("3. Первый запуск и WiFi", h2)]
    s += steps([
        "Включите гирлянду (на секунду мелькнут красный, зелёный, синий). При первом запуске лента <b>мигает синим</b>: открыт портал, сеть <font name='DV-M'>ESP Config</font>.",
        "Подключитесь к ней с телефона (если страница не открылась — <font name='DV-M'>192.168.1.1</font>) и выберите режим:",
    ])
    s += bl([
        "<b>WiFi settings</b> — имя и пароль домашней сети (обычный вариант: гирлянда будет в вашей сети);",
        "<b>Access Point</b> — гирлянда создаёт свою сеть <font name='DV-M'>GyverTwink</font>; <b>придумайте пароль 8–31 символ</b> (адрес гирлянды — <font name='DV-M'>192.168.4.1</font>);",
        "<b>Exit Portal</b> — выйти без изменений (точка <font name='DV-M'>GyverTwink</font> с запасным паролем <font name='DV-M'>12345678</font>).",
    ])
    s += [P("Позже портал открывается, если нажать кнопку в течение 2 секунд после включения (так меняют сеть или пароль).", base),
          Spacer(1, 3),
          table([
              ["Лента", "Значение", "Лента", "Значение"],
              ["мигает синим", "открыт портал", "мигнула зелёным", "подключилась к роутеру"],
              ["мигает жёлтым", "подключается к роутеру", "мигнула красным", "не удалось: создаст свою точку"],
              ["мигнула розовым", "создана точка доступа", "", ""],
          ], [33, 54, 33, 60])]

    s += [P("4. Управление: веб-интерфейс и приложение", h2),
          P("<b>Веб (без приложения).</b> Телефон или компьютер в той же сети: <font name='DV-M'>http://gyvertwink.local</font> или IP гирлянды (виден в роутере и в приложении). "
            "В режиме точки <font name='DV-M'>GyverTwink</font> откройте <font name='DV-M'>http://192.168.4.1</font> (именно http); <font color='#C62828'><b>на Android на время отключите мобильные данные и ответьте «Да» на «Оставаться в сети без интернета».</b></font>", base),
          Spacer(1, 3),
          P("<b>Приложение.</b> Скачайте <font name='DV-M'>GyverTwink-2.0.0.apk</font> со страницы Releases репозитория (тег <font name='DV-M'>app-v2.0.0</font>), разрешите установку из неизвестных источников, "
            "поставьте. Оно само найдёт гирлянды в сети (или введите адрес вручную; точка доступа — кнопка «192.168.4.1»), запомнит последнюю и дальше открывает то же, что и веб. "
            "Кнопка «Сменить гирлянду» в шапке страницы возвращает к списку. Ставьте APK только из этого репозитория.", base),
          Spacer(1, 3),
          table([
              ["Вкладка", "Что там"],
              ["Управление", "питание, яркость, следующий эффект, автосмена (по кругу или случайно) и период, таймер выключения, <b>число светодиодов</b>, лимит тока, порядок цветов ленты"],
              ["Эффекты", "29 эффектов: выбрать = сразу стал текущим; «избранное» (участвует в автосмене), масштаб, скорость; у «Огня v2» ещё форма и цвета пламени"],
              ["Сцены", "8 мест: сохранить текущие настройки (по желанию и схему светодиодов) и вернуть одним нажатием"],
              ["Расписание", "включать и выключать по времени, закату или рассвету, по дням недели (время берётся из интернета или с вашего устройства)"],
              ["Калибровка / Координаты", "положение светодиодов: съёмка камерой или схема по числам; правка точек, масштаб карты (два пальца / колесо)"],
              ["Прошивка", "обновление по WiFi, защита доступа токеном"],
          ], [38, 142])]

    s += [P("5. Калибровка: чтобы эффекты шли по форме гирлянды", h2),
          P("Нужна один раз после развески и при каждом перемещении. Сначала на вкладке «Управление» укажите <b>точное число светодиодов</b>.", base)]
    s += [P("Вариант А — камерой телефона (приложение)", ParagraphStyle("h3", parent=base, fontName="DV-B", spaceBefore=4, spaceAfter=2))]
    s += steps([
        "Закрепите телефон неподвижно, <b>вся гирлянда в кадре</b>, в комнате темно (уберите другие источники света).",
        "Вкладка «Калибровка» → «Включить камеру» (разрешите доступ) → проверьте кадр → «Снять и разобрать». Серия идёт сама (около минуты на 300 светодиодов); не двигайте телефон.",
        "Откроются «Координаты»: проверьте точки, при необходимости перетащите, нажмите <b>«Сохранить на гирлянду»</b>.",
    ])
    s += [P("Вариант Б — по видео (браузер или приложение). «Запустить серию» → у вас есть время включить запись видео в камере (другое устройство тоже подойдёт) → "
            "после конца серии остановите запись → выберите видео → «Разобрать видео». Запись должна начаться <b>до первой вспышки</b>.", base),
          P("<b>Вариант В — без камеры:</b> «Координаты» → схема <b>«спираль»</b> (для ленты, намотанной на цилиндр или конус: введите число светодиодов в каждом витке) "
            "или схема <b>«соты»</b> (файл из Hex Router Designer). «Построить схему» → проверить → «Сохранить».", base),
          P("Если светодиоды найдены неточно: темнее комната, камера неподвижна, верное число светодиодов, шаг серии побольше. Не найденные светодиоды (вне кадра) оцениваются по соседям и отмечены пунктиром. "
            "Живая камера в приложении — новая функция: если на вашем телефоне она не заработала, используйте вариант Б или В.", note)]

    s += [P("6. Кнопка на гирлянде", h2),
          table([
              ["Действие", "Результат"],
              ["1 клик", "включить / выключить"],
              ["2 клика", "следующий эффект"],
              ["удержание", "плавно изменить яркость (направление меняется после каждого отпускания)"],
              ["3 быстрых клика", "открыть на 5 минут окно обновления прошивки и смены токена (лента мигнёт голубым)"],
              ["нажать в течение 2 с после включения", "портал настройки WiFi"],
          ], [60, 120])]

    s += [KeepTogether([P("7. Защита доступа (по желанию)", h2)] + bl([
        "По умолчанию любой в вашей сети может управлять гирляндой. Токен: нажмите кнопку 3 раза, вкладка «Прошивка» → «Защита доступа» → «Сгенерировать» → «Сохранить». Страница будет просить токен на новом устройстве.",
        "Приложение для Android токен вводит на странице, как браузер. Управление по старому UDP-протоколу можно закрыть галочкой «Закрыть управление из приложения (UDP)». Токен идёт по HTTP без шифрования: это защита от случайных гостей, а не от прослушивания.",
        "Не пробрасывайте порт 80 гирлянды в интернет. Забыли токен — снова 3 клика и задайте новый.",
    ]))]

    s += [KeepTogether([P("8. Если что-то не работает", h2),
          table([
              ["Проблема", "Что сделать"],
              ["Гирлянда не находится", "телефон и гирлянда в одной сети? В режиме точки подключитесь именно к <font name='DV-M'>GyverTwink</font>. Введите адрес вручную в приложении"],
              ["<font name='DV-M'>gyvertwink.local</font> не открывается", "используйте IP гирлянды (роутер, приложение)"],
              ["Нужно сменить сеть или пароль точки", "включите гирлянду и нажмите кнопку в течение 2 секунд — откроется портал"],
              ["Обновление по WiFi: «окно закрыто»", "нажмите кнопку 3 раза подряд; у окна 5 минут"],
              ["Цвета перепутаны", "«Порядок цветов ленты» на вкладке «Управление» (у 12-вольтовых лент часто BRG)"],
              ["Эффекты «не по форме» после калибровки", "повторите: неподвижный телефон, темно, верное число светодиодов"],
              ["Гирлянда перезагружается или мерцает", "не хватает питания: уменьшите яркость, включите лимит тока, проверьте питание и общую землю"],
              ["Приложение «Нет связи с гирляндой»", "проверьте питание и сеть; «Повторить» или «Другая гирлянда»"],
          ], [62, 118])])]

    doc.build(s)


if __name__ == "__main__":
    build(sys.argv[1] if len(sys.argv) > 1 else "docs/QUICKSTART.pdf")
