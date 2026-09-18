"""Trombone bell reflection + transmission filters, from Smyth & Scott (2011).

Source: Tamara Smyth and Frederick S. Scott, "Trombone Synthesis by Model and
Measurement", EURASIP Journal on Advances in Signal Processing 2011:151436,
doi:10.1155/2011/151436 (open access, CC-BY). Fetched and read this run from
https://asmp-eurasipjournals.springeropen.com/counter/pdf/10.1155/2011/151436.pdf
(13 pages, 2 481 426 bytes). Full transcription of what was taken from it lives
in docs/autonomy/dsp/reports/2026-09-18-trombone1.md.

WHAT THE PAPER GIVES, AND WHAT IT DOES NOT
------------------------------------------
GIVES (transcribed verbatim, §4.2 / Tables 1-2):

  (21)  a(x) = b (x + x0)^(-gamma)         the measured bell profile, Bessel horn

  Table 1 - "Parameters for Bessel horn described by (21) best fitting the
  trombone bell":
      Length of the bell (m)          .502
      Radius at bell mouth (m)        .108
      Radius at small end (m)         .01
      Bell flare constant   gamma     .7
      Position of the bell mouth (m)  x0   .0174
      Fitting parameter     b         0.0063

  Table 2 - trombone tubular sections, length cm / radius cm:
      t. inner slide (1)      70.8    0.69
      t. outer slide, ext (2) 53      0.72
      slide crook (3)         17.7    0.74
      b. outer slide, ext (4) 53      0.72
      b. inner slide (5)      71.1    0.69
      Gooseneck (6)           24.1    0.71
      Tuning slide (7)        25.4    0.75, 1.07
      Bell flare (8)          56.7    1, 10.8

  (22)-(25) the bell as a piecewise connection of N conical sections, each a
  2x2 scattering matrix A_n on the traveling-wave pair, multiplied into one
  final matrix P.
  (26)  R_B = p1-/p1+ = lambda^2 (P21 + P22 RL) / (P11 + P12 RL)
  (27)  T_B = p_N+ lambda TL / p1+ = lambda TL / (P11 + P12 RL)
  where RL, TL are the open-end reflection/transmission at the bell mouth and
  lambda the propagation loss. The paper takes RL/TL from the Levine &
  Schwinger approximation, "suitable expressions found in [12]" (Smyth & Abel,
  Acta Acustica 95(6):1093-1103, 2009).

  §4.2 also states the bell here is modelled with EIGHT conical sections.

DOES NOT GIVE: any numeric filter. Figures 8, 13 and 16 are raster plots with
no accompanying data table, and reference [12]'s RL/TL expressions are in a
paywalled journal I could not fetch. So NO COEFFICIENT IN THIS FILE IS READ OFF
A CURVE. What ships is the paper's own model, Eq. 21 + Table 1 geometry, solved
here, with two documented substitutions:

  SUB 1 (method, exact): instead of Eqs. 22-25's conical traveling-wave
  scattering matrices I cascade the standard pressure/volume-velocity transfer
  matrices of many short CYLINDERS along the same profile. The paper itself
  names piecewise-cylindrical as an accepted alternative to piecewise-conical
  for exactly this job ("either based on Webster's equation or a piecewise
  connection of cylindrical or conical sections corresponding to the bell's
  profile"), and both converge to the Webster solution. Convergence is
  MEASURED in main() - 8, 50, 400, 2000 slices - so the substitution is
  checkable rather than asserted.

  SUB 2 (termination, labelled): reference [12] is unreachable, so RL/TL come
  from textbook radiation impedances rather than from the paper's citation.
  Two are implemented and both are reported: the exact flanged-piston
  (baffled) impedance Z/Zc = 1 - 2J1(2ka)/(2ka) + j 2H1(2ka)/(2ka), and the
  Levine & Schwinger low-frequency limit Z/Zc = (ka)^2/4 + j 0.6133 ka. The
  bell's own flare dominates both - main() prints the difference so the size
  of this substitution is on the record.

Wall losses use the standard Kirchhoff/Benade thin-boundary-layer attenuation
  alpha = (1/a) sqrt(pi f nu)/c (1 + (gamma-1)/sqrt(Pr))    nepers/m
which reproduces Benade's rule of thumb 2.96e-5 sqrt(f)/a to three digits
(checked in main()).

OUTPUT: minimum-phase biquad cascades fitted to |R_B| and |T_B| at 48 kHz, for
use as (a) the reflection filter at the bore termination, inside the loop, and
(b) the transmission filter on the radiated path. Magnitude-only fitting is
what the paper prescribes for its own filters: "these elements tend to be
minimum phase... a match in magnitude would yield a match in phase".

Usage: python tools/bell_smyth.py        (prints tables, writes the json)
"""
import json
import math
import os

import numpy as np
from scipy.optimize import least_squares
from scipy.special import jv, struve

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
OUT = os.path.join(ROOT, "docs", "research", "nonlinear_bore",
                   "smyth_bell_fit.json")

# --- PHYSICAL: air, 20 C -----------------------------------------------------
C_AIR = 343.0          # m/s
RHO = 1.2041           # kg/m^3
NU = 1.5e-5            # m^2/s   kinematic viscosity
PR = 0.71              # Prandtl
GAM = 1.4              # ratio of specific heats

# --- PHYSICAL: Smyth & Scott Table 1 (measured trombone bell) ----------------
BELL_LEN = 0.502       # m
BELL_MOUTH_R = 0.108   # m
BELL_SMALL_R = 0.01    # m
BELL_GAMMA = 0.7       # flare constant
BELL_X0 = 0.0174       # m
BELL_B = 0.0063        # fitting parameter

SR_FIT = 48000.0
FLO, FHI = 30.0, 12000.0
PASSIVE_CEIL_DB = -0.05      # the fitted reflection may not exceed this
# the fit orders the trombone patch actually ships (gen_trombone1.py):
# 3 reflection sections (in-loop, member-budget bound) and 5 transmission.
SHIP_K_REFL, SHIP_K_TRANS = 2, 4


def bessel_radius(x):
    """Eq. 21: a(x) = b (x + x0)^-gamma, x = distance from the horn MOUTH."""
    return BELL_B * (x + BELL_X0) ** (-BELL_GAMMA)


def alpha_np_per_m(f, a):
    """Thin-boundary-layer wall attenuation, nepers/m, radius a."""
    return (np.sqrt(np.pi * f * NU) / C_AIR) * (1.0 + (GAM - 1.0)
                                                / math.sqrt(PR)) / a


def rad_impedance(f, a, kind="piston"):
    """Radiation impedance at a mouth of radius a, normalized by rho c / S."""
    k = 2.0 * np.pi * f / C_AIR
    ka = k * a
    if kind == "piston":
        # exact baffled-piston (Rayleigh) impedance
        x = 2.0 * ka
        r = 1.0 - 2.0 * jv(1, x) / np.where(x == 0, 1e-12, x)
        xi = 2.0 * struve(1, x) / np.where(x == 0, 1e-12, x)
        return r + 1j * xi
    if kind == "ls":
        # Levine & Schwinger low-frequency limit for the unflanged pipe
        return (ka ** 2) / 4.0 + 1j * 0.6133 * ka
    raise ValueError(kind)


def bell_response(f, n_slices=400, term="piston", losses=True):
    """Solve the measured bell profile as a cascade of n_slices cylinders.

    Returns (R_B, T_rad) where
      R_B    = pressure reflection seen looking INTO the bell from the bore,
               referenced to the bore's own characteristic impedance
      T_rad  = far-field radiated pressure per unit incident wave p+, up to a
               constant (a distance factor); the SHAPE is what is used.
    """
    f = np.atleast_1d(np.asarray(f, dtype=float))
    w = 2.0 * np.pi * f
    # slice from the SMALL end (bore side, x = BELL_LEN) to the MOUTH (x = 0)
    edges = np.linspace(BELL_LEN, 0.0, n_slices + 1)
    centers = 0.5 * (edges[:-1] + edges[1:])
    radii = bessel_radius(centers)
    seg = abs(edges[1] - edges[0])

    # M maps [P;U] at the bore side to [P;U] at the mouth: [P1;U1] = M [Pn;Un]
    m11 = np.ones_like(f, dtype=complex)
    m12 = np.zeros_like(f, dtype=complex)
    m21 = np.zeros_like(f, dtype=complex)
    m22 = np.ones_like(f, dtype=complex)
    for a in radii:
        s = np.pi * a * a
        zc = RHO * C_AIR / s
        k = w / C_AIR
        if losses:
            al = alpha_np_per_m(f, a)
            kc = k + al * (1.0 - 1j)      # lossy wavenumber (phase + decay)
        else:
            kc = k.astype(complex)
        kl = kc * seg
        ca, sa = np.cos(kl), np.sin(kl)
        a11, a12 = ca, 1j * zc * sa
        a21, a22 = 1j * sa / zc, ca
        m11, m12, m21, m22 = (m11 * a11 + m12 * a21, m11 * a12 + m12 * a22,
                              m21 * a11 + m22 * a21, m21 * a12 + m22 * a22)

    s_mouth = np.pi * BELL_MOUTH_R ** 2
    zr = rad_impedance(f, BELL_MOUTH_R, term) * (RHO * C_AIR / s_mouth)
    # [P1;U1] = M [Pn;Un], Pn = Zr Un
    den = m11 * zr + m12
    zin = den / (m21 * zr + m22)
    s_bore = np.pi * BELL_SMALL_R ** 2
    zc_bore = RHO * C_AIR / s_bore
    rb = (zin - zc_bore) / (zin + zc_bore)
    # p+ = (P1 + Zc U1)/2  ->  Un = P1/den, and P1 = p+(1 + rb)
    un_per_pplus = (1.0 + rb) / den
    # Two honest readings of "what comes out of the bell", both textbook:
    #  axis  - far-field ON-AXIS pressure, p ~ j w rho U / (2 pi r).  This is
    #          the closest match to what Smyth & Scott actually measured (a
    #          mic 7 cm off the bell, on axis) and it keeps rising to the top
    #          of the band.
    #  room  - total RADIATED POWER, P ~ |U|^2 Re{Z_rad}, so the magnitude
    #          transfer is |U| sqrt(Re{Z_norm}).  Same +6 dB/oct rise while
    #          ka < 1, then FLAT, because above ka ~ 1 a piston radiates all
    #          the power it is given and only the directivity keeps
    #          narrowing.  This is the right transfer for a listener in a
    #          room rather than a mic in the bell, and it is the one that
    #          ships - a trombone only sounds like the on-axis curve if it is
    #          pointed at your head.
    trad_axis = 1j * w * RHO * un_per_pplus / (2.0 * np.pi)
    zr_norm = rad_impedance(f, BELL_MOUTH_R, term)
    trad_room = np.abs(un_per_pplus) * np.sqrt(np.maximum(zr_norm.real, 1e-12))
    return rb, trad_axis, trad_room


# --------------------------------------------------------------- biquad fit --
def rbj_lp(fc, q, sr=SR_FIT):
    w0 = 2 * np.pi * fc / sr
    c, s = np.cos(w0), np.sin(w0)
    al = s / (2 * q)
    a0 = 1 + al
    return (np.array([(1 - c) / 2, 1 - c, (1 - c) / 2]) / a0,
            np.array([1.0, -2 * c / a0, (1 - al) / a0]))


def rbj_hp(fc, q, sr=SR_FIT):
    w0 = 2 * np.pi * fc / sr
    c, s = np.cos(w0), np.sin(w0)
    al = s / (2 * q)
    a0 = 1 + al
    return (np.array([(1 + c) / 2, -(1 + c), (1 + c) / 2]) / a0,
            np.array([1.0, -2 * c / a0, (1 - al) / a0]))


def rbj_peak(fc, gain_db, q, sr=SR_FIT):
    a = 10.0 ** (gain_db / 40.0)
    w0 = 2 * np.pi * fc / sr
    al = np.sin(w0) / (2 * q)
    b = np.array([1 + al * a, -2 * np.cos(w0), 1 - al * a])
    aa = np.array([1 + al / a, -2 * np.cos(w0), 1 - al / a])
    return b / aa[0], aa / aa[0]


def sections_from(params, k, kind):
    fc, q = 10.0 ** params[0], params[1]
    secs = [rbj_lp(fc, q) if kind == "lp" else rbj_hp(fc, q)]
    for i in range(k):
        pfc, pg, pq = params[2 + 3 * i: 5 + 3 * i]
        secs.append(rbj_peak(10.0 ** pfc, pg, pq))
    return secs


def stack_db(params, k, kind, fgrid):
    w = 2 * np.pi * fgrid / SR_FIT
    z = np.exp(-1j * w)
    h = np.full_like(z, 10.0 ** (params[-1] / 20.0), dtype=complex)
    for b, a in sections_from(params, k, kind):
        h *= (b[0] + b[1] * z + b[2] * z * z) / (1 + a[1] * z + a[2] * z * z)
    return 20.0 * np.log10(np.abs(h) + 1e-30)


def fit_stack(fgrid, target_db, kind, k=2, fc0=800.0):
    x0 = [math.log10(fc0), 0.7]
    lb = [math.log10(80.0), 0.2]
    ub = [math.log10(8000.0), 4.0]
    for i in range(k):
        x0 += [math.log10(np.geomspace(300, 5000, max(k, 1))[i]), 0.0, 1.5]
        lb += [math.log10(FLO), -24.0, 0.2]
        ub += [math.log10(FHI), 24.0, 10.0]
    x0.append(0.0)
    lb.append(-40.0)
    ub.append(40.0)
    res = least_squares(lambda p: stack_db(p, k, kind, fgrid) - target_db,
                        x0, bounds=(lb, ub), max_nfev=6000)
    return res


def as_json_sections(params, k, kind):
    g0 = 10.0 ** (params[-1] / 20.0)
    out = []
    for j, (b, a) in enumerate(sections_from(params, k, kind)):
        bb = b * g0 if j == 0 else b
        out.append([round(float(v), 10)
                    for v in (bb[0], bb[1], bb[2], a[1], a[2])])
    return out


def biquad_nodes(node_prefix, sections, source):
    """MForce Biquad nodes for a fitted cascade. Returns (nodes, last_id)."""
    nodes, src = [], source
    for i, s in enumerate(sections):
        nid = "%s%d" % (node_prefix, i)
        nodes.append({"id": nid, "type": "Biquad", "params": {
            "source": src, "mode": 0, "frequency": 220.0, "radius": 0.0,
            "b0": s[0], "b1": s[1], "b2": s[2], "a1": s[3], "a2": s[4]}})
        src = {"ref": nid}
    return nodes, nodes[-1]["id"]


def design(k_refl=2, k_trans=2, n_slices=400, term="piston", trans="room"):
    """The shipped design: fitted sections + the curves they were fitted to."""
    fg = np.geomspace(FLO, FHI, 400)
    rb, tr_axis, tr_room = bell_response(fg, n_slices, term)
    tr = tr_room if trans == "room" else tr_axis
    rdb = 20.0 * np.log10(np.abs(rb) + 1e-12)
    tdb = 20.0 * np.log10(np.abs(tr) + 1e-30)
    tdb = tdb - tdb.max()                       # shape only; gain set by caller
    rres = fit_stack(fg, rdb, "lp", k_refl, 800.0)
    tres = fit_stack(fg, tdb, "hp", k_trans, 600.0)
    rfit = stack_db(rres.x, k_refl, "lp", fg)
    tfit = stack_db(tres.x, k_trans, "hp", fg)
    refl_secs = as_json_sections(rres.x, k_refl, "lp")
    # PASSIVITY: a fitted reflection that overshoots 0 dB anywhere would hand
    # the loop gain the physics does not have. Scale the cascade down to a
    # peak of exactly PASSIVE_CEIL and report the cost (it is a fraction of a
    # dB — the fit ripple, not the model).
    over = cascade_db(refl_secs, fg).max()
    scale = 10.0 ** ((PASSIVE_CEIL_DB - over) / 20.0) if over > \
        PASSIVE_CEIL_DB else 1.0
    refl_secs = [[s[0] * scale, s[1] * scale, s[2] * scale, s[3], s[4]]
                 if i == 0 else s for i, s in enumerate(refl_secs)]
    rfit = rfit + 20.0 * math.log10(scale)
    return {
        "passivity_scale_db": 20.0 * math.log10(scale),
        "f": fg, "refl_db": rdb, "trans_db": tdb,
        "refl_fit_db": rfit, "trans_fit_db": tfit,
        "refl_sections": refl_secs,
        "trans_sections": as_json_sections(tres.x, k_trans, "hp"),
        "refl_err": rfit - rdb, "trans_err": tfit - tdb,
        "refl_params": rres.x, "trans_params": tres.x,
    }


def cascade_db(sections, fgrid):
    w = 2 * np.pi * fgrid / SR_FIT
    z = np.exp(-1j * w)
    h = np.ones_like(z, dtype=complex)
    for s in sections:
        h *= (s[0] + s[1] * z + s[2] * z * z) / (1 + s[3] * z + s[4] * z * z)
    return 20.0 * np.log10(np.abs(h) + 1e-30)


def main():
    print("=== Smyth & Scott 2011 trombone bell, solved ===")
    print("Bessel horn self-check (Table 1 must reproduce its own radii):")
    print("  a(x=0)        = %.5f m   (Table 1 bell mouth  0.108)"
          % bessel_radius(0.0))
    print("  a(x=0.502)    = %.5f m   (Table 1 small end   0.010)"
          % bessel_radius(BELL_LEN))
    print("wall-loss check @1 kHz, a=1 cm: %.5f np/m  (Benade 2.96e-5 sqrt(f)/a"
          " = %.5f)" % (alpha_np_per_m(1000.0, 0.01),
                        2.96e-5 * math.sqrt(1000.0) / 0.01))

    fg = np.geomspace(FLO, FHI, 400)
    print("\n--- SUB 1: slice-count convergence (piecewise cylinders) ---")
    ref, _, _ = bell_response(fg, 2000, "piston")
    refdb = 20 * np.log10(np.abs(ref) + 1e-12)
    for n in (8, 50, 400, 2000):
        rb, _, _ = bell_response(fg, n, "piston")
        d = 20 * np.log10(np.abs(rb) + 1e-12) - refdb
        print("  %5d slices: max |diff| vs 2000 = %6.3f dB" % (n, np.abs(d).max()))

    print("\n--- SUB 2: termination choice ---")
    rp, tp, tpr = bell_response(fg, 400, "piston")
    rl, tl, tlr = bell_response(fg, 400, "ls")
    dr = 20 * np.log10(np.abs(rp) + 1e-12) - 20 * np.log10(np.abs(rl) + 1e-12)
    print("  flanged-piston vs Levine-Schwinger low-ka limit:")
    print("    reflection: max |diff| %.2f dB, median |diff| %.2f dB"
          % (np.abs(dr).max(), np.median(np.abs(dr))))

    rdb = 20 * np.log10(np.abs(rp) + 1e-12)
    print("\n--- the reflection curve (the bore sees this) ---")
    print("  |R_B| at DC extrapolation %.2f dB" % rdb[0])
    half = np.where(rdb < rdb[0] - 3.0)[0]
    if len(half):
        print("  -3 dB point: %.0f Hz   (ICMC'97 quotes a bell reflection "
              "bandwidth near 800 Hz)" % fg[half[0]])
    for hz in (50, 100, 200, 400, 800, 1600, 3200, 6400):
        i = int(np.argmin(np.abs(fg - hz)))
        print("    %6d Hz  refl %7.2f dB   trans %7.2f dB"
              % (hz, rdb[i], 20 * np.log10(np.abs(tp[i]) + 1e-30)
                 - 20 * np.log10(np.abs(tp).max())))

    d = design(k_refl=SHIP_K_REFL, k_trans=SHIP_K_TRANS, trans="room")
    da = design(k_refl=SHIP_K_REFL, k_trans=SHIP_K_TRANS, trans="axis")
    print("\n--- biquad fits at 48 kHz (the orders the patch ships) ---")
    print("  reflection : %d sections, err median %.2f dB  max %.2f dB"
          % (len(d["refl_sections"]), np.median(np.abs(d["refl_err"])),
             np.abs(d["refl_err"]).max()))
    print("  transmission (room/power, SHIPPED): %d sections, err median "
          "%.2f dB  max %.2f dB"
          % (len(d["trans_sections"]), np.median(np.abs(d["trans_err"])),
             np.abs(d["trans_err"]).max()))
    print("  transmission (on-axis, the A/B cell): %d sections, err median "
          "%.2f dB  max %.2f dB"
          % (len(da["trans_sections"]), np.median(np.abs(da["trans_err"])),
             np.abs(da["trans_err"]).max()))
    print("  reflection peak gain over the band: %.3f (must be <= 1 for a "
          "passive loop)" % (10 ** (cascade_db(d["refl_sections"], fg).max()
                                    / 20.0)))
    for n, s in (("refl", d["refl_sections"]),
                 ("trans_room", d["trans_sections"]),
                 ("trans_axis", da["trans_sections"])):
        for i, sec in enumerate(s):
            print("  %s[%d] b %+.8f %+.8f %+.8f  a %+.8f %+.8f"
                  % (n, i, sec[0], sec[1], sec[2], sec[3], sec[4]))

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    json.dump({
        "source": "Smyth & Scott, EURASIP JASP 2011:151436, Eq.21 + Table 1",
        "method": "piecewise-cylindrical transfer matrix, 400 slices, "
                  "flanged-piston termination, thin-boundary-layer losses; "
                  "room = radiated-power reading (shipped), axis = far-field "
                  "on-axis reading (the paper's own)",
        "sr": SR_FIT,
        "bessel_horn": {"length_m": BELL_LEN, "mouth_r_m": BELL_MOUTH_R,
                        "small_r_m": BELL_SMALL_R, "gamma": BELL_GAMMA,
                        "x0_m": BELL_X0, "b": BELL_B},
        "refl_sections": d["refl_sections"],
        "trans_sections_room": d["trans_sections"],
        "trans_sections_axis": da["trans_sections"],
        "fit_err_db": {
            "refl_median": round(float(np.median(np.abs(d["refl_err"]))), 3),
            "refl_max": round(float(np.abs(d["refl_err"]).max()), 3),
            "trans_room_median": round(float(np.median(np.abs(
                d["trans_err"]))), 3),
            "trans_room_max": round(float(np.abs(d["trans_err"]).max()), 3),
            "trans_axis_median": round(float(np.median(np.abs(
                da["trans_err"]))), 3),
            "trans_axis_max": round(float(np.abs(da["trans_err"]).max()), 3)},
        "curve_hz": [round(float(v), 2) for v in d["f"]],
        "refl_db": [round(float(v), 3) for v in d["refl_db"]],
        "trans_room_db": [round(float(v), 3) for v in d["trans_db"]],
        "trans_axis_db": [round(float(v), 3) for v in da["trans_db"]],
    }, open(OUT, "w"), indent=1)
    print("\nwrote", os.path.relpath(OUT, ROOT))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
