// ============================================================
//  SUPPORT MURAL – TÉLÉCOMMANDE CNC TTC450 (forme manette)
// ============================================================
//  La télécommande se glisse par le dessus, écran vers soi, prises USB vers le bas.
//  Deux berceaux tiennent les poignées ; le centre du bas reste LIBRE :
//  les prises USB-C (recharge + ESP32) et l'interrupteur MARCHE restent accessibles,
//  on peut laisser le câble de recharge branché pendant qu'elle est accrochée.
//
//  Impression : à plat, dos sur le plateau, sans supports (rebords à 45°).
//  PLA ou PETG, 0,2 mm, 3 parois, 20 % de remplissage.
//  Fixation : 2 vis Ø4 à tête fraisée + chevilles.
// ============================================================

/* [Télécommande] */
epaisseur_tele = 42;    // coque 38 + façade 4
chanfrein_bas  = 4;     // chanfrein du dos de la télécommande

/* [Support] */
jeu        = 0.8;       // jeu autour de la télécommande
paroi      = 3;         // épaisseur des berceaux
dos        = 4;         // épaisseur de la plaque murale
rebord     = 4;         // le rebord avant recouvre la façade de 4 mm
y_haut     = -34;       // les berceaux montent jusque-là (repère de la télécommande)
x_libre    = 40;        // zone libre au centre (prises USB, interrupteur) : ±40 mm
vis_d      = 4.5;       // passage de vis Ø4
vis_tete   = 9;
vis_pos    = [[-36, -22], [36, -22]];

$fa = 6; $fs = 0.5;
eps = 0.01;

// ---- contour de la télécommande (identique au boîtier) ----
module contour() {
    for (s = [-1, 1]) hull() {
        translate([s * 62, 8])   circle(r = 32);
        translate([s * 70, -38]) circle(r = 22);
    }
    hull() for (s = [-1, 1]) {
        translate([s * 50, 38])  circle(r = 14);
        translate([s * 36, -40]) circle(r = 12);
    }
}

// zone des berceaux : bas des poignées, hors du centre
module zone_berceaux() {
    difference() {
        translate([-200, -200]) square([400, 200 + y_haut]);
        translate([-x_libre, -200]) square([2 * x_libre, 400]);
    }
}

module plaque() {
    linear_extrude(dos) difference() {
        offset(r = jeu + paroi) intersection() {
            contour();
            translate([-200, -200]) square([400, 200 - 10]);   // plaque jusqu'à Y = -10
        }
    }
}

module berceaux() {
    h = epaisseur_tele + jeu;
    // parois qui épousent les poignées
    translate([0, 0, dos - eps]) linear_extrude(h + eps)
        intersection() {
            difference() { offset(r = jeu + paroi) contour(); offset(r = jeu) contour(); }
            zone_berceaux();
        }
    // rebord avant chanfreiné à 45° (imprimable sans support)
    n = 8;
    for (i = [0 : n - 1]) {
        z = dos + h + i * rebord / n;
        translate([0, 0, z]) linear_extrude(rebord / n + eps)
            intersection() {
                difference() {
                    offset(r = jeu + paroi) contour();
                    offset(r = jeu - (i + 1) * rebord / n) contour();
                }
                zone_berceaux();
            }
    }
}

difference() {
    union() { plaque(); berceaux(); }
    // vis fraisées
    for (p = vis_pos) translate([p[0], p[1], -1]) {
        cylinder(d = vis_d, h = dos + 2);
        translate([0, 0, 1 + dos - (vis_tete - vis_d)/2]) cylinder(d1 = vis_d, d2 = vis_tete, h = (vis_tete - vis_d)/2 + eps);
    }
    // allègement + passage des câbles derrière la zone centrale
    translate([0, -48, -1]) linear_extrude(dos + 2)
        offset(r = 4) square([2 * x_libre - 26, 14], center = true);
}
