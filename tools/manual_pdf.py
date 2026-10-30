#!/usr/bin/env python3
"""Build the SID manual PDF. Authoring dependency: reportlab (not an app dependency)."""
from pathlib import Path
import argparse
import re
from html import escape
import subprocess
import textwrap

from reportlab.lib import colors
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle
from reportlab.platypus import (SimpleDocTemplate, Paragraph, Spacer, Image,
                               PageBreak, Table, TableStyle, Preformatted)

ROOT = Path(__file__).resolve().parents[1]
BLUE = colors.HexColor('#352879')
INK = colors.HexColor('#202035')
MUTED = colors.HexColor('#56566b')


def plain(text):
    return text.replace('\u2013', '-').replace('\u2014', '-').replace('\u2011', '-')


def inline(text):
    # The source manual uses paragraphs, headings, code fences, links and emphasis.
    tokens = re.split(r'(`[^`]+`|\*\*[^*]+\*\*|\[[^\]]+\]\([^)]+\))', plain(text))
    result = []
    for token in tokens:
        if token.startswith('`'):
            result.append('<font name="Courier" size="9">'+escape(token[1:-1])+'</font>')
        elif token.startswith('**'):
            result.append('<b>'+escape(token[2:-2])+'</b>')
        elif token.startswith('[') and '](' in token:
            label, target = token[1:-1].split('](', 1)
            result.append('<link href="'+escape(target, quote=True)+'" color="#352879">'+escape(label)+'</link>')
        else:
            result.append(escape(token))
    return ''.join(result)


def build(output):
    output = output.resolve()
    if not output.is_relative_to((ROOT/'.local').resolve()):
        raise ValueError('Generated PDFs belong under .local/')
    output.parent.mkdir(parents=True, exist_ok=True)
    revision = subprocess.check_output(['git', 'rev-parse', '--short', 'HEAD'], cwd=ROOT, text=True).strip()
    body = ParagraphStyle('Body', fontName='Helvetica', fontSize=10.5, leading=15.2,
                          textColor=INK, spaceAfter=10, splitLongWords=True)
    heading = ParagraphStyle('Heading', parent=body, fontName='Helvetica-Bold',
                             fontSize=21, leading=26, textColor=BLUE, spaceBefore=14,
                             spaceAfter=16, keepWithNext=True)
    small = ParagraphStyle('Small', parent=body, fontSize=9, leading=13, textColor=MUTED)
    title = ParagraphStyle('Title', parent=heading, fontSize=38, leading=43, spaceBefore=30)
    code = ParagraphStyle('Code', fontName='Courier', fontSize=8, leading=12,
                          backColor=colors.HexColor('#f1eff8'), borderPadding=10,
                          spaceBefore=6, spaceAfter=18)
    doc = SimpleDocTemplate(str(output), pagesize=A4, rightMargin=48, leftMargin=48,
                            topMargin=55, bottomMargin=48, title='GTracker - SID Manual',
                            author='GTracker', subject='SID tracker controls, arranging, playback and export')
    width = A4[0]-96
    story = [Image(str(ROOT/'docs/assets/gtracker-banner.png'), width=width, height=width/3),
             Paragraph('SID MANUAL', title),
             Paragraph('GTracker - The Ultimate SID Tracker', heading),
             Paragraph('Three voices. Still enough.', body), Spacer(1, 18),
             Paragraph('Write notes. Arrange patterns. Play the SID. Export the tune.', body),
             Paragraph('For the 0.5.0 release candidate / Windows and Linux', small),
             Paragraph('Manual edition: 30 October 2026<br/>Source revision: '+revision, small),
             Spacer(1, 20),
             Paragraph('Start here', heading)]
    for item in ('1. Extract the portable ZIP and open Start SID Tracker.cmd (Windows) or ./start-sid.sh (Linux).',
                 '2. Open examples/sid/heavy-mellow.sidtrk.json. Press F5 to hear the song.',
                 '3. Use Save As to keep your own version. F6 loops a pattern; F8 stops playback.'):
        story.append(Paragraph(escape(item), body))
    story += [Spacer(1, 14), Paragraph('This manual includes the complete SID guide, a keyboard reference and export setup. The guitar editor has a separate guide. macOS release verification is deferred.', small),
              Paragraph('<link href="https://timosaarinen.github.io/gtracker/" color="#352879">Website</link> / <link href="https://github.com/timosaarinen/gtracker/releases" color="#352879">Downloads</link> / <link href="https://github.com/timosaarinen/gtracker/issues" color="#352879">Report a bug</link>', small), PageBreak(),
              Paragraph('Keyboard reference', heading),
              Paragraph('Click a NOTE cell to enter music. Text fields and dialogs take keyboard focus while you use them.', body)]
    rows = [('Key', 'Action'), ('Z S X D C V G B H N J M', 'Piano keys: current octave'),
            ('Q 2 W 3 E R 5 T 6 Y 7 U', 'Piano keys: octave above'), ('A / Delete', 'Note-off / clear event or selection'),
            ('F5 / F6', 'Play song from beginning / loop pattern'), ('F7 / Space', 'Start at selected row; Space then pauses/resumes'),
            ('F8', 'Stop and cancel pending playback'), ('Tab / Shift+Tab', 'Next / previous voice'),
            ('Home / End', 'First / last pattern row'), ('Page Up / Page Down', 'Move 16 rows'),
            ('Shift+Arrows', 'Select a block'), ('Ctrl+C / X / V', 'Copy / cut / paste'),
            ('Ctrl+Z / Ctrl+Y', 'Undo / redo (Ctrl+Shift+Z also redoes)'),
            ('Ctrl+S / Ctrl+Shift+S', 'Save / Save As'), ('Ctrl+O', 'Open SID song'),
            ('Escape', 'Toggle Edit outside dialogs; cancel inside dialogs'),
            ('Numpad / and *', 'Change octave'), ('Numpad - and +', 'Change preset')]
    table = Table([[Paragraph(escape(a), small), Paragraph(escape(b), small)] for a,b in rows],
                  colWidths=[width*.43, width*.57], repeatRows=1, hAlign='LEFT')
    table.setStyle(TableStyle([('BACKGROUND',(0,0),(-1,0),colors.HexColor('#e6e1f3')),
        ('VALIGN',(0,0),(-1,-1),'TOP'),('LEFTPADDING',(0,0),(-1,-1),9),
        ('RIGHTPADDING',(0,0),(-1,-1),9),('TOPPADDING',(0,0),(-1,-1),7),
        ('BOTTOMPADDING',(0,0),(-1,-1),7),('LINEBELOW',(0,0),(-1,-1),.35,colors.HexColor('#d9d5e5'))]))
    story += [table, Spacer(1, 12), Paragraph('The source guide also documents Cmd equivalents. Pattern speed is global; section names do not change sound or tempo.', small), PageBreak()]
    text = (ROOT/'docs/sid-tracker.md').read_text(encoding='utf-8')
    # Preserve every source paragraph; keep short adjacent topics together.
    for block in re.split(r'\n\s*\n', text.strip()):
        if block.startswith('# '):
            story.append(Paragraph('Getting started', heading))
        elif block.startswith('## '):
            if block[3:] not in ('Enter and arrange music', 'MP4 sharing export'):
                story.append(PageBreak())
            story.append(Paragraph(inline(block[3:]), heading))
        elif block.startswith('```'):
            lines = block.splitlines()[1:-1]
            wrapped = '\n'.join(textwrap.fill(plain(line), 91, subsequent_indent='  ',
                                break_long_words=False, break_on_hyphens=False) for line in lines)
            story.append(Preformatted(wrapped, code))
        else:
            story.append(Paragraph(inline(' '.join(block.splitlines())), body))

    def page(canvas, document):
        canvas.saveState()
        canvas.setStrokeColor(colors.HexColor('#d9d5e5'))
        canvas.line(48, A4[1]-35, A4[0]-48, A4[1]-35)
        canvas.setFont('Helvetica-Bold', 8)
        canvas.setFillColor(BLUE)
        canvas.drawString(48, A4[1]-25, 'GTRACKER / SID MANUAL')
        canvas.setFont('Helvetica', 8)
        canvas.setFillColor(MUTED)
        canvas.drawString(48, 27, '0.5.0 RC / 30 OCT 2026')
        canvas.drawRightString(A4[0]-48, 27, str(document.page))
        canvas.restoreState()
    doc.build(story, onFirstPage=page, onLaterPages=page)
    print(output)


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT/'.local/manual/gtracker-sid-manual.pdf')
    build(parser.parse_args().output)
