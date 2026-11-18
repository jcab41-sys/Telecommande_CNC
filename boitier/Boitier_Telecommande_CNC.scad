// ============================================================
//  TÉLÉCOMMANDE CNC (TTC450) – BOÎTIER FORME MANETTE – OpenSCAD
//  Écran 2,8" tactile + ESP32-S3 Freenove
// ============================================================
//  Tenue à deux mains comme une manette de jeu :
//   pouce gauche -> joystick X/Y          pouce droit -> joystick Z
//   au centre    -> écran tactile 2,8" + 3 touches (ZÉRO XY, ZÉRO Z, PAUSE)
//   poignée gauche -> potentiomètre AVANCE %   poignée droite -> bouton ARRÊT
//   bord avant   -> USB-C ESP32, USB-C recharge, interrupteur MARCHE
//   batterie     -> 18650 dans un support imprimé avec la coque (2 ressorts), sous l'écran
//
//  Pièces à exporter (changer "piece", F6 puis F7 pour le STL) :
//   "coque", "couvercle" (déjà retourné), "etiquettes" (2e couleur CFS),
//   "touches" (3 capuchons MX), "tout" (vue d'ensemble, ne pas imprimer)
//
//  Cotes reprises de tes pupitres (validées) :
//   - joystick : couvercle du pupitre V6 (passage Ø28 décalé +1/+1,
//     chanfreins 2,5, pions Ø7 à 21 x 27, carte à 10,75 mm sous la plaque)
//   - écran 2,8" : pupitre V4 "ecran28" (fenêtre 59 x 46 décalée de 2,5 mm,
//     entraxe 76 x 45, pions de 3,5 mm sous la lèvre, vis M2.5)
//     -> l'écran se monte dans le même sens que sur le pad rouge
//  Valeurs marquées (*) : à vérifier au pied à coulisse.
// ============================================================

piece = "tout"; // [tout, coque, couvercle, etiquettes, touches]

/* [Boîtier] */
H          = 38;    // hauteur de la coque (sans la façade) – 38 : pile 18650 + câbles Dupont debout sur l'ESP32
paroi      = 2.4;
fond       = 2.0;
chanfrein_bas = 4;
jeu        = 0.25;

/* [Façade – identique aux pupitres] */
epaisseur_couvercle = 4;
arrondi_couvercle   = 2;
h_levre             = 1.75;
ep_levre            = 1.6;

/* [Joystick KY-023 – pupitre V6] */
joy_entraxe           = [21, 27];
joy_decal_axe         = [1, 1];
joy_pcb               = [26, 34];
d_joystick            = 28;
chanfrein_joystick    = 2.5;
hauteur_pion_joystick = 9;
d_pion_joystick       = 7;
d_vis_joystick        = 2.8;
joy_axe_x             = 61;
joy_axe_y             = 9;
conge_pion            = 2.5;   // congé au pied des pions, comble l'entaille du chanfrein du dessous
// Plat des pions côté manche : la même profondeur sur les 4 pions, celle du pion
// le plus entamé par le passage Ø28 (calculée automatiquement)
profondeur_plat = max([for (sx = [-1, 1], sy = [-1, 1])
    d_joystick/2 + d_pion_joystick/2 - norm([sx * joy_entraxe[0]/2 - joy_decal_axe[0], sy * joy_entraxe[1]/2 - joy_decal_axe[1]])]);
echo(str("Plat des pions du joystick : ", profondeur_plat, " mm"));

/* [Écran 2,8" – pupitre V4] */
ecran_c          = [0, 20];      // centre des trous de fixation
ecran_fenetre    = [59, 46];
ecran_fenetre_shift_x = 2.5;     // fenêtre décalée par rapport aux trous
ecran_entraxe    = [76, 45];
hauteur_pion_ecran = 3.5;        // sous la lèvre (comme le pupitre V4)
d_pion_ecran     = 7;
d_vis_ecran      = 2.2;          // M2.5
ecran_pcb        = [83, 52];     // contrôle seulement (*)
chanfrein_ecran  = 1.2;
rayon_fenetre    = 1.5;

/* [Touches mécaniques (switchs MX)] */
touche_pas = 22;
touche_y   = -20;
mx_trou    = 14.0;
mx_plaque  = 1.5;

/* [Potentiomètre AVANCE % – pupitre V4] */
pot_c          = [-60, -40];
d_potentiometre = 8;
angle_ergot    = 180;     // ergot anti-rotation (trou borgne Ø3 à 7,5 mm de l'axe)

/* [Bouton ARRÊT, voyant, interrupteur MARCHE] */
stop_c   = [60, -40];
stop_d   = 11.8;          // filetage mesuré ~11,2 mm + jeu
ep_fixation = 2;          // plaque amincie à 2 mm autour du bouton
voyant_c = [0, -44];
d_led_voyant = 5.2;
inter    = [19.2, 12.7];  // interrupteur à bascule (découpe, comme le pupitre V6)
inter_x  = 27;            // sur le bord avant, à droite des prises USB

/* [Électronique dans la coque] */
// Freenove ESP32-S3-WROOM montée À L'ENVERS : broches vers le haut pour brancher
// les câbles Dupont par le dessus ; elle repose sur son module et ses prises USB-C.
esp_dim   = [27.8, 57.6]; // largeur (sur les barrettes) x longueur (antenne comprise), mesurées
esp_comp  = 3.2;          // hauteur des composants sous la carte (prises USB-C, module) (*)
esp_cale  = 1.8;          // cales sous le module et sous les prises USB
usb_esp   = [27, 6.4];    // ouverture pour les 2 prises USB-C (comme le pupitre V6)
// trous dans le fond pour appuyer sur BOOT et RESET avec un trombone (vus du dessus) (*)
bouton_boot = [ 9.2, 12.5];   // [x depuis l'axe de la carte, y depuis l'avant des prises USB] (mesuré : 18 mm entre les 2 boutons)
bouton_rst  = [-9.2, 12.5];
tp_dim   = [17.5, 28];    // chargeur TP4056 USB-C (HW-373)
tp_x     = -28;
/* [Support de pile 18650 imprimé (intégré à la coque)] */
pile_d        = 18.6;   // diamètre de la pile (*)
pile_l        = 65.2;   // longueur de la pile, téton + compris (*)
pile_jeu      = 0.4;    // jeu autour de la pile
pile_y        = 36;     // axe de la pile (en haut, sous l'écran, hors de l'ESP32)
ressort_d     = 7;      // ressort de compression 7 x 12,5 mm (assortiment 200 pièces)
ressort_comprime = 6;   // 12,5 mm libre -> environ 6 mm une fois la pile en place
ressort_logement = 2;   // profondeur du logement qui centre le ressort
paroi_bout    = 3;      // épaisseur des parois d'extrémité
pince_pile    = 3;      // les bords du berceau montent 3 mm au-dessus de l'axe : la pile se clipse

/* [Vis M3 fraisées] */
d_pilier       = 8;
d_pretrou_m3   = 2.8;
vis_trou_couv  = 3.4;
vis_tete       = 6.5;

/* [Textes] */
police     = "Liberation Sans:style=Bold";
texte_prof = 0.6;

$fa = 6; $fs = 0.5;
eps = 0.01;

// ============================================================
//  CONTOUR "MANETTE"
// ============================================================
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
module contour_retrait(d) { offset(r = -d) contour(); }

colonnes = [[-84, 8], [84, 8], [-56, 34], [56, 34], [-74, -50], [74, -50]];

joy_axes = [[-joy_axe_x, joy_axe_y], [joy_axe_x, joy_axe_y]];
function pions(axe) = let (c = axe - joy_decal_axe)
    [for (sx = [-1, 1], sy = [-1, 1]) [c[0] + sx * joy_entraxe[0]/2, c[1] + sy * joy_entraxe[1]/2]];
pions_ecran = [for (sx = [-1, 1], sy = [-1, 1])
    [ecran_c[0] + sx * ecran_entraxe[0]/2, ecran_c[1] + sy * ecran_entraxe[1]/2]];

touches = [
    [-touche_pas, touche_y, "ZÉRO XY"],
    [0,           touche_y, "ZÉRO Z"],
    [ touche_pas, touche_y, "PAUSE"],
];

// ============================================================
//  COQUE DU BAS
// ============================================================
module volume_chanfreine(d, z0, h) {
    n = ceil(chanfrein_bas / 0.5);
    for (i = [0 : n - 1]) {
        z = i * chanfrein_bas / n;
        if (z + chanfrein_bas / n > z0)
            translate([0, 0, max(z, z0)])
                linear_extrude(chanfrein_bas / n + eps)
                    contour_retrait(d + chanfrein_bas - z - chanfrein_bas / (2 * n));
    }
    translate([0, 0, max(chanfrein_bas, z0)]) linear_extrude(h - max(chanfrein_bas, z0)) contour_retrait(d);
}

module fente(l, h, prof) {  // fente horizontale à bouts ronds, axe Y
    rotate([90, 0, 0]) linear_extrude(prof, center = true)
        hull() for (s = [-1, 1]) translate([s * (l/2 - h/2), 0]) circle(d = h);
}

module coque() {
    y_bas = -52;
    usb_z_esp = fond + esp_cale + esp_comp / 2;   // prises USB sous la carte retournée
    usb_z_tp  = fond + 2 + 1.6 + 1.6;
    difference() {
        union() {
            difference() {
                volume_chanfreine(0, 0, H);
                volume_chanfreine(paroi, fond, H + 1);
            }
            intersection() {
                for (c = colonnes) translate([c[0], c[1], 0]) cylinder(d = d_pilier, h = H);
                linear_extrude(H) contour_retrait(0.5);
            }
            support_esp();
            // plots du chargeur
            tp_yc = y_bas + paroi + tp_dim[1]/2 + 0.5;
            for (sx = [-1, 1], sy = [-1, 1])
                translate([tp_x + sx * (tp_dim[0]/2 - 1.5), tp_yc + sy * (tp_dim[1]/2 - 1.5), fond])
                    cylinder(d = 3, h = 2);
            support_pile();
        }
        for (c = colonnes) translate([c[0], c[1], fond + 1]) cylinder(d = d_pretrou_m3, h = H);
        // bord avant : 2 USB-C de l'ESP32-S3, USB-C de recharge, interrupteur MARCHE
        translate([0, y_bas, usb_z_esp]) fente(usb_esp[0], usb_esp[1], paroi * 4);
        trous_boot_reset();
        translate([tp_x, y_bas, usb_z_tp]) fente(11, 6, paroi * 4);
        translate([inter_x, y_bas, H/2]) cube([inter[0], paroi * 4, inter[1]], center = true);
    }
}


// ============================================================
//  SUPPORT DE L'ESP32-S3 (carte retournée, broches vers le haut)
// ============================================================
esp_y0 = -52 + paroi + 0.3;                         // bord USB de la carte, contre la paroi avant
z_dessous_pcb = fond + esp_cale + esp_comp;          // dessous du circuit
module support_esp() {
    l = esp_dim[0] + 0.6;
    // cale sous les prises USB (7 premiers mm) et cale sous le module (côté antenne)
    translate([-l/2, esp_y0, fond - eps]) cube([l, 7, esp_cale + eps]);
    translate([-9, esp_y0 + esp_dim[1] - 22, fond - eps]) cube([18, 21, esp_cale + eps]);
    // guides latéraux (la carte se pose entre eux, ruban adhésif mousse sous le module)
    for (s = [-1, 1]) translate([s * (l/2 + 0.6), esp_y0 + 12, fond + (z_dessous_pcb + 1.6 - fond)/2 - eps])
        cube([1.2, 30, z_dessous_pcb + 1.6 - fond], center = true);
}
module trous_boot_reset() {     // dans le fond, sous les boutons de la carte retournée
    for (b = [bouton_boot, bouton_rst])
        translate([-b[0], esp_y0 + b[1], -1]) cylinder(d = 3.5, h = fond + 2);   // x inversé : carte retournée
    // repères lisibles par-dessous
    for (b = [[bouton_boot, "BOOT"], [bouton_rst, "RST"]])
        translate([-b[0][0], esp_y0 + b[0][1] + 5, -eps]) mirror([1, 0, 0]) linear_extrude(0.5)
            text(b[1], size = 2.5, font = police, halign = "center", valign = "center");
}

// ============================================================
//  SUPPORT DE PILE 18650 (imprimé avec la coque)
// ============================================================
//  Berceau en U : la pile se clipse par le dessus. Un ressort à chaque bout,
//  centré dans un logement ; son fil passe par un trou dans la paroi et se
//  soude derrière (jamais sur la pile). Encoche au milieu pour la sortir au doigt.
pile_r     = (pile_d + pile_jeu) / 2;
pile_axe_z = fond + 0.6 + pile_r;                       // axe de la pile
pile_int   = pile_l + 2 * ressort_comprime;             // entre les deux fonds de ressort
module support_pile() {
    long_ext = pile_int + 2 * paroi_bout;
    larg     = pile_d + pile_jeu + 2 * 2.4;
    h_bords  = pile_axe_z + pince_pile;
    h_bouts  = pile_axe_z + ressort_d / 2 + 2;
    translate([0, pile_y, 0]) difference() {
        union() {
            // berceau
            translate([-pile_int/2, -larg/2, fond - eps]) cube([pile_int, larg, h_bords - fond]);
            // parois d'extrémité (portent les ressorts)
            for (s = [-1, 1]) translate([s > 0 ? pile_int/2 : -pile_int/2 - paroi_bout, -larg/2, fond - eps])
                cube([paroi_bout, larg, h_bouts - fond]);
        }
        // logement de la pile
        translate([-pile_int/2, 0, pile_axe_z]) rotate([0, 90, 0])
            cylinder(r = pile_r, h = pile_int);
        // dégagement au-dessus (entrée de la pile)
        translate([-pile_int/2 - eps, -(pile_r - 0.6), h_bords]) cube([pile_int + 2*eps, 2 * (pile_r - 0.6), 30]);
        for (s = [-1, 1]) {
            // logement du ressort dans la paroi d'extrémité
            translate([s * pile_int/2, 0, pile_axe_z]) rotate([0, s * 90, 0])
                translate([0, 0, -eps]) cylinder(d = ressort_d + 0.4, h = ressort_logement);
            // trou pour le fil du ressort
            translate([s * (pile_int/2 + paroi_bout/2), 0, pile_axe_z]) rotate([0, 90, 0])
                cylinder(d = 1.8, h = paroi_bout + 2 + 2 * ressort_logement, center = true);
        }
        // encoche pour sortir la pile au doigt
        translate([0, -larg/2, h_bords]) rotate([90, 0, 0]) cylinder(d = 16, h = 10, center = true);
        // repères + / - gravés sur le haut des parois d'extrémité
        translate([ pile_int/2 + paroi_bout/2, 0, h_bouts - 0.6]) linear_extrude(1)
            text("+", size = 2.6, font = police, halign = "center", valign = "center");
        translate([-pile_int/2 - paroi_bout/2, 0, h_bouts - 0.6]) linear_extrude(1)
            text("-", size = 2.6, font = police, halign = "center", valign = "center");
    }
}

// ============================================================
//  FAÇADE (z = 0 sous la plaque, dessus à z = 4)
// ============================================================
module plaque() {
    e = epaisseur_couvercle; R = arrondi_couvercle;
    linear_extrude(e - R + eps) contour();
    for (a = [0 : 15 : 75])
        translate([0, 0, e - R + R * sin(a)])
            linear_extrude(R * (sin(a + 15) - sin(a)) + eps)
                contour_retrait(R - R * cos(a + 15));
    translate([0, 0, -h_levre])
        difference() {
            linear_extrude(h_levre + eps) contour_retrait(paroi + jeu);
            translate([0, 0, -1]) linear_extrude(h_levre + 2) contour_retrait(paroi + jeu + ep_levre);
            for (c = colonnes) translate([c[0], c[1], -1]) cylinder(d = d_pilier + 1.2, h = h_levre + 2);
        }
}

module texte(t, p, taille = 3) {
    translate(p) text(t, size = taille, font = police, halign = "center", valign = "center");
}
module etiquettes_2d() {
    for (t = touches) texte(t[2], [t[0], t[1] - 13.3]);
    texte("X / Y", [-joy_axe_x, joy_axe_y - 21.5], 3.4);
    texte("Z",     [ joy_axe_x, joy_axe_y - 21.5], 3.4);
    texte("AVANCE %", [pot_c[0],  pot_c[1] + 13.5], 3.2);
    texte("ARRÊT",    [stop_c[0], stop_c[1] + 13.5], 3.6);
}
module etiquettes_volume() {
    translate([0, 0, epaisseur_couvercle - texte_prof]) linear_extrude(texte_prof + 1) etiquettes_2d();
}

module pions_joystick() {
    for (a = joy_axes, p = pions(a))
        translate([p[0], p[1], -h_levre - hauteur_pion_joystick])
            cylinder(h = hauteur_pion_joystick + h_levre + 0.1, d = d_pion_joystick);
}
module passage_joystick() {   // passage Ø28 + chanfreins dessus et dessous (pupitre V6)
    e = epaisseur_couvercle;
    for (a = joy_axes) translate([a[0], a[1], 0]) {
        translate([0, 0, -h_levre - hauteur_pion_joystick - 2])
            cylinder(h = e + h_levre + hauteur_pion_joystick + 4, d = d_joystick);
        translate([0, 0, e - chanfrein_joystick])
            cylinder(h = chanfrein_joystick + 0.1, d1 = d_joystick, d2 = d_joystick + 2 * chanfrein_joystick);
        translate([0, 0, -h_levre - 0.1])
            cylinder(h = chanfrein_joystick + 0.1, d1 = d_joystick + 2 * chanfrein_joystick, d2 = d_joystick);
    }
}

module couvercle_brut() {
    e = epaisseur_couvercle;
    difference() {
        union() {
            difference() {
                union() {
                    plaque();
                    for (p = pions_ecran)
                        translate([p[0], p[1], -h_levre - hauteur_pion_ecran])
                            cylinder(h = hauteur_pion_ecran + h_levre + 0.1, d = d_pion_ecran);
                }
                passage_joystick();
            }
            // Pions du joystick : gardent le plat du passage Ø28 (le joystick ne frotte pas),
            // mais PAS l'entaille du chanfrein du dessous : un congé la comble à leur pied.
            difference() {
                union() {
                    pions_joystick();
                    for (a = joy_axes, p = pions(a)) translate([p[0], p[1], 0]) {
                        translate([0, 0, -conge_pion])
                            cylinder(h = conge_pion + eps, d1 = d_pion_joystick, d2 = d_pion_joystick + 2 * conge_pion);
                        cylinder(h = chanfrein_joystick - h_levre + 0.05, d = d_pion_joystick + 2 * conge_pion);  // jusqu'au haut du chanfrein du dessous, pas plus
                    }
                }
                for (a = joy_axes) translate([a[0], a[1], -h_levre - hauteur_pion_joystick - 2])
                    cylinder(h = epaisseur_couvercle + h_levre + hauteur_pion_joystick + 4, d = d_joystick);
                // même plat sur chaque pion, face au manche
                for (a = joy_axes, p = pions(a))
                    translate([p[0], p[1], -h_levre - hauteur_pion_joystick - 1])
                        rotate([0, 0, atan2(a[1] - p[1], a[0] - p[0])])
                            translate([d_pion_joystick/2 - profondeur_plat, -10, 0])
                                cube([20, 20, h_levre + hauteur_pion_joystick + 1 + chanfrein_joystick]);
            }
        }
        for (a = joy_axes, p = pions(a))
            translate([p[0], p[1], -h_levre - hauteur_pion_joystick - 1])
                cylinder(h = hauteur_pion_joystick + 1, d = d_vis_joystick);

        // écran (pupitre V4)
        translate([ecran_c[0] + ecran_fenetre_shift_x, ecran_c[1], 0]) {
            translate([0, 0, -h_levre - hauteur_pion_ecran - 2])
                linear_extrude(e + h_levre + hauteur_pion_ecran + 4)
                    offset(r = rayon_fenetre) offset(delta = -rayon_fenetre) square(ecran_fenetre, center = true);
            hull() {
                translate([0, 0, e - chanfrein_ecran]) linear_extrude(eps)
                    offset(r = rayon_fenetre) offset(delta = -rayon_fenetre) square(ecran_fenetre, center = true);
                translate([0, 0, e + eps]) linear_extrude(0.5)
                    offset(r = rayon_fenetre + chanfrein_ecran) offset(delta = -rayon_fenetre)
                        square(ecran_fenetre, center = true);
            }
        }
        for (p = pions_ecran)
            translate([p[0], p[1], -h_levre - hauteur_pion_ecran - 1])
                cylinder(h = hauteur_pion_ecran + 1, d = d_vis_ecran);

        // switchs MX
        for (t = touches) translate([t[0], t[1], 0]) {
            translate([0, 0, -h_levre - 1]) linear_extrude(e + h_levre + 2) square(mx_trou, center = true);
            translate([0, 0, -h_levre - 1]) linear_extrude(h_levre + 1 + e - mx_plaque) square(15.6, center = true);
        }

        // potentiomètre + ergot (pupitre V4)
        translate([pot_c[0], pot_c[1], -h_levre - 2]) cylinder(h = e + h_levre + 4, d = d_potentiometre);
        translate([pot_c[0] + 7.5 * cos(angle_ergot), pot_c[1] + 7.5 * sin(angle_ergot), -h_levre - 1])
            cylinder(h = 2.5 + h_levre, d = 3);

        // bouton ARRÊT
        translate([stop_c[0], stop_c[1], -h_levre - 1]) {
            cylinder(d = stop_d, h = e + h_levre + 2);
            cylinder(d = 18, h = h_levre + 1 + e - ep_fixation);
        }
        // voyant
        translate([voyant_c[0], voyant_c[1], -h_levre - 2]) cylinder(h = e + h_levre + 4, d = d_led_voyant);

        // vis M3 fraisées
        for (c = colonnes) translate([c[0], c[1], 0]) {
            translate([0, 0, -h_levre - 1]) cylinder(d = vis_trou_couv, h = e + h_levre + 2);
            translate([0, 0, e - (vis_tete - vis_trou_couv)/2])
                cylinder(d1 = vis_trou_couv, d2 = vis_tete, h = (vis_tete - vis_trou_couv)/2 + eps);
        }
    }
}

module couvercle()  { difference()   { couvercle_brut(); etiquettes_volume(); } }
module etiquettes() { intersection() { couvercle_brut(); etiquettes_volume(); } }
module a_plat() { translate([0, 0, epaisseur_couvercle]) rotate([0, 180, 0]) children(); }

// ============================================================
//  CAPUCHONS DE TOUCHES (axe MX en croix)
// ============================================================
module carre_arrondi(c, r, h) { linear_extrude(h) offset(r = r) offset(delta = -r) square(c, center = true); }
module capuchon() {
    h = 8;
    difference() {
        hull() { carre_arrondi(18, 2, 0.1); translate([0, 0, h - 0.1]) carre_arrondi(14.5, 2, 0.1); }
        translate([0, 0, -eps]) hull() { carre_arrondi(16, 1.2, 0.1); translate([0, 0, h - 1.7]) carre_arrondi(12.5, 1.2, 0.1); }
    }
    difference() {
        translate([0, 0, 0.5]) cylinder(d = 5.6, h = h - 1.5);
        cube([4.2, 1.4, 8], center = true);
        cube([1.4, 4.2, 8], center = true);
    }
}
module capuchons_plateau() {
    for (i = [0 : 2]) translate([i * 22, 0, 8]) rotate([180, 0, 0]) capuchon();
}

// contrôle : place entre le dessus du support 18650 et le dos de l'écran
dos_ecran = H - h_levre - hauteur_pion_ecran - 1.6;
echo(str("Pile : dessus à Z = ", pile_axe_z + pile_r, " ; parois des ressorts jusqu'à Z = ", pile_axe_z + ressort_d/2 + 2,
         " ; dos de l'écran à Z = ", dos_ecran, " ; support de ", pile_int + 2*paroi_bout, " mm de long"));

// ============================================================
//  SÉLECTION
// ============================================================
if (piece == "coque")           coque();
else if (piece == "couvercle")  a_plat() couvercle();
else if (piece == "etiquettes") a_plat() etiquettes();
else if (piece == "touches")    capuchons_plateau();
else {
    color("dimgray") coque();
    translate([0, 0, H + 0.5]) {
        color("orange") couvercle();
        color("black") etiquettes();
        for (t = touches) translate([t[0], t[1], epaisseur_couvercle + 5]) color("white") capuchon();
    }
}
