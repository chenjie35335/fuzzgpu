
import matplotlib.pyplot as plt
########################
# Set fonts for figures
########################
FONTSIZE = 7
plt.rcParams['font.family'] = 'serif'
plt.rcParams['font.serif'] = ['Times New Roman'] + plt.rcParams['font.serif']
plt.rcParams['font.size'] = FONTSIZE
plt.rcParams['axes.labelsize'] = FONTSIZE
plt.rcParams['axes.titlesize'] = FONTSIZE
plt.rcParams['legend.fontsize'] = FONTSIZE
plt.rcParams['xtick.labelsize'] = FONTSIZE
plt.rcParams['ytick.labelsize'] = FONTSIZE

# Set axes spines (figure border) to dark gray
plt.rcParams['axes.edgecolor'] = '#404040'
plt.rcParams['axes.linewidth'] = 0.5

plt.rcParams['pdf.fonttype'] = 42
plt.rcParams['ps.fonttype'] = 42

textwidth_pt = 505 *0.5 # \textwidth: pt
textwidth_in = textwidth_pt / 72  # inch

MY_COLOR_MAPS1 = [
    (0,0,0), # k
    (65,14,115), # p
    (140,42,129),
    (223,74,104),
    (252,154,107),
    (252,248,187) # yellow
]
MY_COLOR_MAPS1 = [(t[0]/255, t[1]/255, t[2]/255) for t in MY_COLOR_MAPS1]

# lighter
MY_COLOR_MAPS2 = ['#d73027',
'#fc8d59',
'#fee090',
'#91bfdb',
'#e0f3f8',
'#4575b4',
]

#deeper
MY_COLOR_MAPS3 = ['#d73027',
'#fc8d59',
'#fee090',
'#91bfdb',
'#e0f3f8',
'#4575b4',
]


MY_COLOR_MAPS4 = [
    '#4D4D4D', # k
    '#F5BF4C', # yellow
    '#4F92CD', # blue
    '#28BA91', # cyan
    '#F05F96', # magenta
]

MY_COLOR_MAPS5 = {
    '#b40105', # orange
    '#e34c3b', # green
    '#fd848d', # blue
    '#e7fe92', # magenta
    '#2066ac', # yellow
}
