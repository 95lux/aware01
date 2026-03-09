import matplotlib.pyplot as plt

COLOR_STEM = '#aaaaaa'

def styled_stem(ax, x, y):
    markerline, stemlines, baseline = ax.stem(x, y)
    baseline.set_color(COLOR_STEM)
    baseline.set_linewidth(0.8)
    stemlines.set_color(COLOR_STEM)
    stemlines.set_linestyle(':')
    stemlines.set_linewidth(0.9)
    markerline.set_color('black')
    markerline.set_markerfacecolor('white')
    markerline.set_markersize(6)
    markerline.set_markeredgewidth(1.5)