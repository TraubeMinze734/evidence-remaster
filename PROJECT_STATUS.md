# evidence-remaster — Projektstatus & Fallstricke

Stand: nach PR #9 (Theme-Stability). Alles hier Genannte ist auf dem
Ziel-Rechner (CachyOS, GCC 14, aktuelles EFL aus den Distri-Repositories)
getestet.

## Erledigt und funktioniert

### Build und Install
- `configure.sh` mit pkg-config-Erkennung, `make`, `make install` nach
  `/usr/local`. Binary, Themes, Icon-Sets, Plugins, Theme-Engines,
  `.desktop`-Datei (mit `evidence.xpm` als Icon) und der Imlib2-Bild-
  Thumbnailer werden korrekt installiert.
- Icon-Sets: indigo_128, tosh_128, efm, rephormed, fishtank_128, LCARS,
  gorilla_36/48/72, ento_64. Die 2005-Tarballs `tosh_96` und `indigo_96`
  enthielten **keine einzigen Bilddateien** (nur Mapping/README/Skript);
  die Install-Routine entfernt leere Sets automatisch.
- Themes tosh, efm, fishtank, nautilus, rephormed (default), LCARS sind
  installierbar und stabil. **metallo und OtE werden NICHT installiert**
  (2005-Binärformate: metallo.bg.db braucht Ebg, OtE/default.eet nutzt
  Edje-Gruppen `Bg`/`Fg`, die moderne Edje nicht mehr lädt). Beide fallen
  auf `default` zurück statt zu crashen. `fonts` ist ein Daten-Ordner
  (Schriften), kein Theme.

### GTK2-GUI
- Kern und GTK2-Frontend laufen auf GCC 14, GTK 2.24. Icon-View mit
  Evas-Canvas (Buffer-Engine mit Cairo-Blit, da die alten X11-Engine-Header
  aus EFL entfernt wurden). Alle installierbaren Themes nutzbar,
  z.B. `--theme tosh`.

### EFL-Port
- Gradient-Kompat-Schicht `evas_compat.c` ersetzt die aus EFL entfernte
  `evas_object_gradient`-API. Rubberband, Scrollbars, Tooltips und
  Icon-Frames rendern originalgetreu. Das gevas-Widget wurde vom
  GTK1-Type-System auf GType portiert.
- Emotion-Thumbnailer (Video): statische Vorschauen funktionieren.
  Der statische Pfad rendert über die **Buffer-Engine in einen eigenen
  ARGB32-dest_buffer** (zeilenweise, Stride w*4), NICHT über
  `emotion_object_image_get()` (dessen Daten bei laufender Pipeline
  Rauschen liefern). Nach dem Seek 0,5 s Settle-Zeit vor dem Render.

### Features
- Live-Video im Tooltip über Emotion/GStreamer, Ton und Bild, mehrfach
  wiederholbar, sauberes Schliessen. Tooltips mit View-Abdunkelung und
  Metadaten (`--enable-tooltips` oder Mittelklick). Vier Tooltip-Fabriken
  (traditional/centre/diagram/wholewindow), per Theme wählbar — das
  fensterfüllende Verhalten von `diagram` ist Original-Design von 2005.
- **Echte Thumbnails**: Videos (Emotion, Frame bei ~8 %) und Bilder
  (Imlib2-Plugin `imlib.so`, das anfangs gar nicht gebaut wurde). Der
  freedesktop-Cache liegt in `~/.thumbnails/` (normal/large/fail), NICHT
  in `~/.evidence/thumbnails/`.
- Kontextmenü mit Delete, Rename, New folder, Properties. Doppelklick
  öffnet Dateien über xdg-open. inotify aktualisiert Views bei
  Verzeichnisänderungen — inkl. Live-Rename im Icon-View (PR #11:
  inotify-Namen gegen Watch-Dir aufgelöst, DEL/NEW-Events mit korrekten
  Pfaden im Modell, Double-Unref bei DEL behoben). libextractor liefert Metadaten im Tooltip
  (GStreamer-Plugin davon ausgenommen, Reentrancy-Probleme). libcurl und
  libmagic aktiv. Animierte Icon-Effekte, Rubberband-Selektion, Drag&Drop.
- Action-Plugins komplett: neben delete/rename/mkdir/info_dial/newwindow/
  backdrop/tile jetzt auch tag2name, name2tag, sortkey2name, playlist,
  searchable, searchable_r. Die vorher toten Eintraege aus evidence.menu
  (Rename from Ogg/ID3/APE-tag, tag from file-name, Rename from sortkey,
  Make playlist, searchable) funktionieren wieder. tag2name linkt mit
  GTK2-CFLAGS (inkludiert gtk/gtk.h), die restlichen brauchen nur Core.
  ACHTUNG: tag2name/name2tag/sortkey2name setzen sortkey-/Tag-Metadaten
  voraus — die spezialisierten Metadata-Provider (mp3/vorbis/ape/exif/ttf/
  image) sind noch nicht portiert, libextractor deckt das weitgehend ab.
- searchable/searchable_r: libdoodle gibt es nicht mehr in Arch/AUR.
  search.c hat deshalb im Nicht-Doodle-Build einen search_scan_dir-
  Stub (NOIR_NOTSUPP), damit die Plugins laden und eine klare Meldung
  kommen statt "undefined symbol". Wird libdoodle je wieder verfuegbar,
  HAVE_DOODLE in configure.sh erkennen und der Stub verschwindet.
- playlist.so: menu_file_context() setzt bei Einzelklick NUR ac->single,
  die Selection-Liste bleibt leer. playlist.c iteriert aber nur ueber
  die Selection. Fix in playlist.c: bei leerer Selection das Single-File
  per action_file_new(ac,tag) in die Selection aufnehmen. Das generische
  Audio-Icon sagt nichts ueber den MIME aus (kommt aus mime.icons
  Wildcards); MIME selbst bestimmen mime.types/libmagic.
- Clipboard (PR #12, clipboard.c): Strg+C/X legt die Selection jetzt zusaetzlich
  auf die CLIPBOARD-Selection als x-special/gnome-copied-files (copy/cut-
  Payload) UND text/uri-list, mit text/plain als Fallback. Damit verstehen
  Dolphin/Nautilus/Thunar evidence-Kopien als Dateien, nicht als Text.
  Strg+V im View pastet ueber file_job_create() (CP bei copy, MV bei cut)
  ins cwd derselben Job-Maschinerie wie Drag&Drop. Der alte "(yet)"-Alert
  ist weg. Der Typebuffer-Pfad (Strg+V bei offenem Typebuffer) unveraendert.
  Statische Tag-Snapshots in clipboard.c (clipboard_file_tags) — die
  Selection darf nach Strg+C ruhig weiterleben; das Clipboard liefert die
  URIs on-demand aus dem Snapshot.
  Zwei ownership-Fallstricke (getestet, PR #13):
  1. selection_update() darf die CLIPBOARD-Selection NIE anfassen.
     Ein Clipboard muss Selektionswechsel/Fokusverlust ueberleben; nur
     die Strg+C/X-Handler setzen es. Sonst ist es leer, bevor Dolphin
     (oder man selbst) einfuegt.
  2. file_job_create() uebernimmt das Ownership am action_context
     (file_job_dst() ruft action_context_dst). Aufrufer duerfen den
     Context danach NICHT selbst freen — Double-Free, SIGABRT erst
     NACH erfolgreichem Job-Ende ("dial for job should go away" ->
     free(): invalid pointer). Ausserdem gehoert das ZIEL-Verzeichnis
     als single in den Context (Muster wie file_drop_do), sonst hat
     der Job keinen dst_path.
  3. gtk_clipboard_set_with_data() ruft beim Uebergang des Ownership
     den clear-Handler des VORbesitzers auf — das sind wir selbst.
     Den Snapshot der Dateien erst NACH set_with_data() committen,
     sonst loescht der eigene clear-Handler die frischen Daten und
     jede zweite Strg+C-Kopie liefert ein leeres Clipboard.
  4. Clipboard-URIs sind percent-escapet ("%20" fuer Leerzeichen).
     file_if_local() dekodiert NICHT. Empfangene Pfade aus
     gnome-copied-files/uri-list zwingend percent-dekodieren
     (clipboard_uri_decode in clipboard.c), sonst "Datei nicht
     gefunden" bei Namen mit Spaces.
- Copy-Jobs / Fortschrittsanzeige (PR #13):
  1. file_job_bytes() dividierte MB, labelte aber "GB" — Total zeigte
     GB ab 100MB. Jetzt: GB erst ab 100GB, korrekt /1024^3.
  2. file_job_scan() hatte "job->total_bytes=ch->size" (= statt +=):
     das Total war immer nur das LETZTE Top-Level-Kind, Copied aber
     akkumuliert ueber alles → Balken ueber 100% ("158/138 GB").
  3. Langsamkeit: posix_copy_data() rief file_job_stats() (Mutex +
     GUI-Handshake) nach JEDEM 4K-Block, dir_copy()/dir_move() riefen
     file_job_update(PROGRESS) pro Verzeichniseintrag — jedes Mal wartete
     der Backend-Thread auf den 250ms-GUI-Pulse (file_job_wait4gui).
     Fixes: Stats nur noch ~alle 100ms + einmal final pro Datei;
     dir_copy/dir_move nutzen UNCHANGED-Updates (kein Handshake).
     Kopieren laeuft damit nahe an Platten-/read()-Geschwindigkeit.
  (2. Runde, nach User-Test mit neuem Build noch immer langsam): Das war
  noch nicht alles — file_job_update() wartete bei JEDEM Statuswechsel
  (PROGRESS pro Datei via file_copy/dir_copy, START, ADD beim Prescan)
  im file_job_wait4gui-Handshake auf den GUI-Pulse. Der Handshake ist
  nur fuer EDIT/QUESTION (Antwort noetig) und DONE (Job wird zerstoert)
  Pflicht — jetzt ueberspringen PROGRESS/START/ADD/DEL ihn komplett.
  Ebenfalls unit_size() in util.c gefixt (dividierte MB, labelte GB,
  gleicher Bug wie file_job_bytes): TB/GB/MB-Schwellen korrekt.
  ACHTUNG Folgefehler (Version c2ac366): file_job_wait4gui() NIMMT
  datalock (pthread_cond_wait) — das Funktions-ende von file_job_update()
  gibt ihn immer frei. Wer den Handshake-Skip einbaut, muss fuer die
  Nicht-Handshake-Zweige SELBST locken (else if(job->async)
  pthread_mutex_lock(&job->datalock)), sonst: Unlock eines nie
  genommenen Mutex + ungeschuetzte Writes gegen den GUI-Leser
  (Symptom: Progress-Fenster erscheint gar nicht mehr).
  ZWEITE Stolperfalle (Version c86b83d): Der Handshake-Skip darf START
  NICHT einschliessen — das Progress-Fenster wird ausschliesslich im
  NOIR_JOB_START-Fall von file_job_progress_show() erzeugt. Ohne
  Handshake rennt der Backend-Thread in 250ms am START vorbei, das
  Fenster entsteht nie, alle Folgebetrifft treffen NULL-Slots
  (Symptom: Fenster erscheint auch nach dem Lock-Fix nicht). START
  feuert nur einmal pro Job — Handshake dort ist gratis. EDIT/
  QUESTION/DONE/START = Handshake; PROGRESS/ADD/DEL = nur datalock.
  DRITTE Stolperfalle (cc652bb): Der START-Handshake allein reicht
  NICHT — der Backend setzt START, rennt weiter, und das naechste
  PROGRESS ueberschreibt START, bevor der 250ms-Pulse ihn sieht
  (Fenster nie sichtbar, Pulse loggt "copy progress!" ohne Widgets).
  Der Fast-Path braucht eine gezielte konditionale Wait: PROGRESS/
  ADD/DEL duerfen einen unkonsumierten START nie ueberschreiben
  (while status==START: cond_wait). Kostet eine Pulse-Warte pro Job.
  PLUS Shutdown-Deadlock: file_job_wait_all() joint Threads, die im
  Handshake auf den bereits toten GUI-Pulse warten → SIGKILL noetig.
  Fix: globales jobs_shutdown-Flag — wait4gui und die START-Wait
  warten waehrend des Shutdowns nie; wait_all setzt das Flag, weckt
  per cond_broadcast alle Jobs und joint erst dann.
- Icon-View Reaktivitaet in bilderlasten Verzeichnissen: jedes fertige
  Thumbnail feuerte MODEL_UPD -- THUMB → icon_view_rerender() →
  icon_list_layout_all() (Voll-Layout ALLER Icons). Bei 50+ Bildern
  serienweise Thumbnail-Events = 50+ Voll-Layouts, die die GUI
  blockierten — Klick-Highlight reagierte erst Sekunden spaeter.
  Fix: THUMB-Rerender geht jetzt ueber icon_view_rerender_maybe()
  (throttled auf IV_UPDATE_EVERY, 2000ms) — derselbe Mechanismus,
  den der freiwillige Rerender bereits nutzt. Thumbs erscheinen
  dadurch gebuendelt alle ~2s statt einzeln, dafuer bleibt die GUI
  fluessig. (PR #14)
- EXIF-Metadaten-Provider (src/providers/exif/exif.c, PR #17): Der 2005er
  Code ist ein eigenstaendiger, JHead-adaptierter EXIF-Parser (KEIN libexif
  noetig!). Er wurde nie ins plain-Makefile uebernommen, obwohl
  setup/evidence.providers exif.so fuer image/jpx referenziert. Jetzt
  gebaut als lib/evidence/metadata/exif.so — Plugin-Contract wie folder.so
  (meta_get_for_file + layoutp, meta_plugin_init optional, undef Symbole
  sind Teilmenge der core-Exporte via -rdynamic). Getestet mit handgebautem
  JPEG: Parser liest Model/DateTime/ExposureTime korrekt. WICHTIG beim
  Testen: Der Parser toleriert FirstOffset 8..16 (Pentax-Workaround von
  2002) und warnt nur — Debug-Log "Suspicious offset" ist keine Fehler.
- unit_size() (util.c, zeigt Dateigroessen in Tooltips/Properties): Der
  2005-Code stapelte ZWEI Divisionsschemata — die Einheiten-Auswahl
  dividierte schon, der Formatter dividierte ERNEUT durch 1024. Bei
  KB/MB fiel das zufaellig auf den Fuss zurueck (deshalb 100er-MB
  korrekt), ab 1GB aber: 1GB → auf 1 skaliert → 1/1024 → "0.0 GB".
  Neu: einmal skalieren, einmal formatieren (TB/GB/MB/KB, %.1f).
  Auch file_job_bytes() (PR #13) hatte denselben Doppel-Divisions-Bug.
- Live-Video im Tooltip nach Fenster-Resize (nur Ton, kein Bild):
  gevas_paint() warf JEDE Paint-Area weg, die nicht exakt
  pixels_w x pixels_h entsprach ("resize pending"). Nach einem
  Resize sendet GTK aber Teil-Exposes, deren Area von der
  (bereits neu konfigurierten) Backing-Store-Groesse abweicht —
  alle weiteren Paints wurden fuer immer verworfen. Der
  Live-Video-Thumbnail rendert pro dekodiertem Frame selbst
  (evas_render im frame_decode-Callback), d.h. die Frames lagen im
  Buffer, erreichten den Screen aber nie. Fix: Area an den
  Backing-Store clampen statt verwerfen. Das allein reichte NICHT —
  Debug-Trace bewies: nach dem Resize lieferte GTK KEINE Exposes mehr
  (queue_draw feuerte, gevas_paint kam nie). Fix 2: RENDER_FLUSH_POST
  schedult einen GTK-Idle-Blit (gevas_blit(), NUR cairo, KEIN render —
  render im Idle baute eine paint->render->flush->idle-Repaint-Loop,
  die das Resizing ruckelig machte). Fallstricke dabei: (a) der
  g_idle_add-Handle darf NICHT in ev->current_idle — dort erwarten
  idle_enter_remove/gevas_queue_redraw ein ECORE-Handle, GTK-guint an
  ecore_idle_enterer_del = SIGSEGV (eigener Slot flush_idle). (b)
  Blit + evas_render im frame_decode-Callback auf 33ms/~30fps
  drosseln (GEVAS_FLUSH_INTERVAL / LIVE_FRAME_INTERVAL).
- Exit mit laufendem Copy-Job: evidence.c riss file_exit() (dlclose des
  Backends, Hash-Tabellen weg) und das Modell unter dem noch kopierenden
  Backend-Thread ab → SIGSEGV beim Beenden waehrend eines Jobs. Fix:
  file_job_wait_all() (joint alle laufenden Jobs via file_job_collect)
  VOR file_exit() im Exit-Pfad von evidence.c.
- Progress-Fenster (PR #13): Schliessen per WM-X ist KEIN Cancel — der Job
  laeuft weiter, das Fenster wird nur weggeklickt. Der 2005-destroy-Handler
  (form_modeless_destroyed) setzte nur user1=NULL; user2..user6 (box, label,
  bars) blieben als Zeiger auf zerstoerte Widgets stehen → Use-after-free,
  SIGABRT beim naechsten Progress-Update. Fix: Handler invalidiert alle
  sechs Slots (user1..user6 liegen kontinuierlich im struct, Pointer-
  Arithmetik ab &user1). Die label/bar-Update-Fns tolerieren NULL-Slots
  bereits. Ein echter Cancel-Button existiert (noch) nicht — waere ein
  eigenes Feature (Cancel-Flag im Job + Abbruch im Backend-Loop).
- Versteckte Dateien: `WITH_DOT_DIR` steckt sie in einen virtuellen
  `.files`-Ordner (Tag `/dir/.`). `file_info_insert` legt diesen on-demand
  an, wenn der Scanner ihn noch nicht angelegt hat.

### Warnungen
- Etwa 26 Typsicherheits-Warnungen behoben. 59 Deprecated-Hinweise
  (GTK1-Relikte) bewusst offen gelassen.

## GTK3-Backend (Phase 1+2, PR #18/#19)

Der GTK-Teil baut jetzt gegen **GTK2 (Default) oder GTK3**:

    ./configure.sh                # GTK2, unverändertes 2005er-Verhalten
    ./configure.sh --with-gtk3    # GTK3 (HAVE_GTK3 in config.h)

GTK2 bleibt der getestete Default-Pfad; alle GTK3-Weichen stehen
hinter `GTK_CHECK_VERSION(3,0,0)`-Guards bzw. `HAVE_GTK3`.

### Architektur-Unterschied GTK3 (der wichtigste Punkt!)

- **GTK2:** evidence läuft in der *ecore*-Mainloop; GTK wird über den
  X11-Connection-fd in die ecore-Loop gepumpt (2005er-Trick, X11-only).
- **GTK3:** evidence läuft in `gtk_main()` (GLib-Loop). ecore wird per
  `ecore_main_loop_glib_integrate()` in die GLib-Loop integriert;
  schlägt das fehl (ecore ohne glib-Support gebaut, z.B. Arch/CachyOS),
  übernimmt ein 10ms-GLib-Timer (`ecore_glib_pump`) als Fallback-Pumpe.
  Diagnose: `--debug 2`, dann "ecore rides the GLib mainloop" (Integration
  OK) vs "installing manual pump" (Fallback aktiv).
- GTK3 läuft damit auch unter **Wayland**; alle X11-Zugriffe
  (`get_display()`, `get_root_window()`, `get_desk()`, `window_as_desk()`)
  sind NULL-sicher -- auf Wayland liefert get_display() NULL, Pseudo-
  Transparenz meldet "kein Desktop", statt Xlib zu crashen.

### Die Dispatcher-Krankheit (util.c) -- WICHTIGE FALLE

util.c hat sechs Wrapper mit `HAVE_ECORE`-Präzedenz, die unter GTK3
ALLE falsch lagen, weil dort ecore nicht mehr die GUI-Loop ist. Alle
sechs sind jetzt `HAVE_GTK3`-first:

| Funktion | GTK3 jetzt | Symptom wenn falsch |
|---|---|---|
| `mainloop_quit()` | `gtk_main_quit()` | Exit haengt ewig |
| `mainloop_once()` | `g_main_context_iteration()` | Rechtsklick-Menue friert ein |
| `timeout_add/remove()` | GLib-Timer/`g_source_remove()` | Exit haengt; SIGSEGV beim Start (GLib-ID an ecore) |
| `idle_enter_add/remove()` | GLib-Idle/`g_source_remove()` | dito |
| `events_pending()` | `g_main_context_pending()` | **SIGSEGV**: Arbeiter-Threads (Scanner!) iterierten ecore -> EO-Domain-Korruption, Crash in `ecore_main_loop_begin()` |

Faustregel: **jede neue Code-Stelle, die Mainloop/Timers/Idles dispatcht,
muss HAVE_GTK3 vor HAVE_ECORE pruefen.** Und: `events_pending()` unter
HAVE_ECORE iteriert als Side-Effect die Loop -- nur vom besitzenden
Thread (main_thread-Guard eingebaut), moderne EFL kennt per-Thread-
EO-Domains.

### `#endif`-Falle (5 Vorfälle in dieser Session!)

NIEMALS Code auf dieselbe Zeile wie eine Preprocessor-Direktive
legen: `#endif }` -- die Klammer wird mit der Direktive verschluckt,
die Funktion bleibt offen, der Parser frisst alles Folgende
("ungueltige Speicherklasse fuer Funktion"). Beim Editieren mit
Guards: Klammern immer auf EIGENE Zeile. Vor jedem Commit:
`grep -rn "endif }" src/` + Preprocessor-faehige Klammer-Balance-
Pruefung fuer BEIDE Pfade (GTK2/GTK3).

### GTK3-Port-Einzelheiten (PR #19)

- Keysyms: `gdkkeysyms-compat.h` (GTK3 nennt alles GDK_KEY_*;
  compat-Header in common/keysyms.h eingebunden)
- Menues: `gtk_menu_popup_at_pointer()` statt `gtk_menu_popup()`
  (GTK3/Wayland braucht Anker, sonst haengt die modale Menue-Loop)
- gevas-Widget: draw-vfunc (Cairo clip-extents), get_preferred_*,
  destroy in GtkWidgetClass, realize ohne Colormap/BackPixmap
- Cell-Renderer: GTK3-render-Signatur (cairo_t*), Padding/Alignment/
  FixedSize nur noch ueber Getter; `GtkCellRendererText` ist OPAQUE
  (nur "text" ist ein echtes Property! font/scale/fixed-height sind
  GTK2-Struct-Member -- g_object_get gibt CRITICALs)
- DnD: `GtkSelectionData`/`GdkDragContext` opaque -> Compat-Header
  `src/gui/gtk2/common/gtk_compat.h` (evis_seldata_*(), evis_drag_action)
- GDK_SCROLL_MASK im gevas-realize nachtragen (GTK3 liefert sonst
  keine Mausrad-Events an das Widget)
- GtkWidget->parent privat -> gtk_widget_get_parent()
- GtkTooltips weg -> gtk_widget_set_tooltip_text (info_dial)
- 'Preferences'-Menueeintrag nur wenn externes Tool 'examine'
  existiert (get_path_file-Probe beim Menueaufbau)

### Emotion-Thumbnailer: Stabilitaetssaegen dieser Session

Der Video-Thumbnailer hatte vier Crash/Hanger-Klassen, ALLE
durch Backtraces des Users aufgeklaert:

1. **Timer-Selbstmord:** `_emotion_make_timer` loeschte im Timeout-Pfad
   den Timer, in dessen Callback es lief ("destroying a timer which
   seems dead already") -> Objektfreigabe-Race -> EO-Fehlerflut.
   Fix: `c->timer=NULL` vor dem finish, ecore raeumt bei CANCEL selbst.
2. **Stack-Riss:** thumbnailer_gc pthread_cancel(ASYNCHRONOUS) traf den
   Worker im Handshake, waehrend der ecore_timer im GUI-Thread dessen
   Stack-Frame (ctx ist lokal!) weiter nutzte -> futex-Fehler, SIGABRT.
   Fix: Plugin laeuft mit PTHREAD_CANCEL_DISABLE (deferred).
3. **GUI-Selbst-Deadlock:** cxl/GC jointen den deferred-cancelled Worker
   aus dem GUI-Thread, der aber selbst die Loop fuellen muesste, damit
   der Worker fertig wird. Fix: nach Cancel nur QUEUE_ZOMBIE setzen;
   Join/Restart ueberlaesst thumbnailer_start() (naechster enqueue).
4. **Audio-in-mp4-Endlosschleife:** .m4a/.m4v (nur Audio) wird von
   libmagic als video/mp4 erkannt (gleicher Container) -> Emotion-
   Timeout 10s -> Re-Visit-Retry -> Endlos, ein CPU-Kern bei 100%.
   Fix: Audio-Gate im Plugin (audio/*-MIME + Audio-Extensions ->
   NOIR_NOTSUPP, kein FAIL-Eintrag, generisches Audio-Icon).
5. **Exit-Crash:** emotion_shutdown() deinizialisiert GStreamer
   asynchron; Backend-Threads posteten nach evas_shutdown() in den
   toten Loop -> SIGSEGV in efl_object_shutdown(). Fix: ~0.5s
   Mainloop-Pump-Gnadenfrist im Plugin-Exit.

BEKANNTES RESTVERHALTEN (kein Bug): Dateien mit NUR Audio in Video-
Containern (z.B. .m4v) bekommen nach dem 10s-Timeout KEIN statisches
Thumbnail (generisches Video-Icon + FAIL_ME); der Live-Tooltip spielt
das Audio ab -- korrekt, denn nur der Dekoder weiss, dass kein
Videostream existiert. Feintuning am Seek-Settle spaeter moeglich.

### Icon-View: gemischte Ansicht beim View-Wechsel (gefixt)

Notebook-Wechsel (Icon->Browser->Icon) mit Ordnerwechsel dazwischen
mischte beide Ordner in eine Ansicht: der pre-CWD (icon_list_rst)
erreicht pausierte Views nicht, und `noir_sess_focus` verschluckte
den Nachhole-Fokus ("already in focus()"). Fix: `iv->cwd_shown`
merkt sich den geleerten Ordner; post-CWD raeumt nach, wenn der pre
fuer DEN Ordner nie lief. Offen/harmlos: "GLib-CRITICAL: Source ID
was not found" (Stale-Timeout-Handle beim Page-Switch-Race).

### SIGPIPE (beide Builds!)

`signal(SIGPIPE,SIG_IGN)` in main(): libextractor forked trotz
EXTRACTOR_OPTION_IN_PROCESS Helper-Kinder; stirbt eines, killte das
SIGPIPE den Scanner-Thread (und damit evidence). GUI-Standard.

### GTK2-Regressionstest (nach PR #19): BESTANDEN

Beide Builds (GTK2 und GTK3) laufen stabil auf CachyOS: Start, alle
Views, Menues, Video-Thumbnails, Live-Tooltips, Scrollen, Drag&Drop,
sauberer Exit. Die GTK2-Abstuerze (events_pending/Scanner-Thread)
waren uebrigens derselbe EFL-Threading-Bug wie GTK3 -- kein
GTK2-vs-GTK3-Unterschied, sondern modernes EFL trifft 2005er-Design.

## Kritische Fallstricke, die Änderungen schnell verschlimmbessern würden

1. `file_if_local()` in file.c gibt KEINE Kopie zurück, sondern einen
   Pointer in den Original-String. Niemals freen. Ein solches `free()`
   hat mal alle Datei-Tags des Modells zerstört (Ordner verschwunden,
   Crash).

2. `gtk_signal_connect_object` hat andere Callback-Semantik als
   `g_signal_connect_object` (Slot-Substitution). Niemals als Batch
   ersetzen. Die 59 Deprecated-Warnungen nur einzeln und verifiziert
   anfassen oder gar nicht.

3. `thumbnail_plugin_register_ext()` ist in thumbnail_plugin.h
   deklariert, aber NIEMALS implementiert worden (2005-TODO). Nie aufrufen.

4. Der Thumbnailer-Plugin-Cache in `~/.evidence/thumbnailers/*.lst`
   regeneriert nur, wenn die neueste .so-Datei im Plugin-Verzeichnis
   neuer ist als der Cache. Der Check misst die Datei-mtime, nicht die
   Verzeichnis-mtime (make install überschreibt .so ohne Verzeichnis zu
   ändern). Diese Logik steht in thumbnail_plugin.c, nicht verändern.
   Nach Plugin-Änderungen: `rm ~/.evidence/thumbnailers/*.lst`.

5. Action-Plugins müssen nach `lib/evidence/action/` (SINGULAR), weil
   setup/evidence.menu diese Pfade referenziert. Ein actions-Plural-
   Verzeichnis führt zu NOIR_NOTFOUND (-5) beim Rename-Dialog.

6. `HAVE_EVAS2` muss in den Theme-Engines definiert sein. Die Engines
   includen kein config.h selbst; es kommt über evas_macros.h. Ohne das
   ist der komplette Compat-Layer inklusive Gradient-Clamps ein No-Op
   und EFL spammt premultiplied-Fehler.

7. Evas/Emotion-Objekte duerfen NUR im GUI-Thread erzeugt werden. Der
   Thumbnailer-Worker betreibt einen eigenen Ecore-Loop,
   `eina_main_loop_is()` ist dort TRUE (taugt also NICHT als Test).
   Erkennung über die globale Variable `main_thread` (evidence.c setzt
   sie in main(), via evidence.h exportiert, Binary baut mit -rdynamic).
   Dispatch dorthin mit `ecore_main_loop_thread_safe_call_async` plus
   Mutex/Cond-Handshake (die alte Funktion ecore_main_loop_thread_call
   existiert nicht mehr).

8. **Der Worker MUSS den Imlib-Mutex freigeben, bevor er auf den
   GUI-Thread wartet** (`IMLIB_RELEASE`/`IMLIB_ACQUIRE` um den Handshake
   in thumbnail_plugin_make). Der GUI-Thread braucht denselben Mutex
   (Theme-Engines wie ix_rephormed, Thumbnail-Loads) — mit gehaltenem
   Mutex warten ist ein garantierter ABBA-Deadlock (keine GUI, kein
   Fortschritt, kein Crash).

9. **Imlib-Kontext-Vertrag**: `imlib_create_cropped_image()` macht das
   neue Bild zum aktuellen Kontext. Wer danach freit, gibt das
   THUMBNAIL frei, nicht den Wrapper. Nach allem Crop/Free:
   `imlib_context_set_image()` explizit auf das zurückgegebene Bild
   setzen — `thumbnailer_generate()` ruft `imlib_image_set_format()`
   VOR dem Kontextwechsel auf. Sicherste Variante: gar kein
   using_data-Wrapper, sondern `imlib_create_image()` + memcpy.

10. `emotion_shutdown()` ist ein Refcount, den jedes Loeschen eines
    Video-Objekts dekrementiert. Das Emotion-Plugin hält deshalb eine
    `emotion_init()`-Referenz ueber seine Lebenszeit, sonst wird
    GStreamer mitten im Betrieb deinitialisiert (danach kein playbin
    mehr, Crash). Vor `evas_object_del()` auf Emotion-Objekten:
    `emotion_object_play_set(o,0)` + `emotion_object_file_set(o,NULL)`
    (asynchroner Close rennt sonst gegen das bevorstehende free).

11. Der Thumbnailer-Protected-Mode (fork pro Generierung) muss aus
    bleiben (Property thumbnail-protected-mode, Default 0). EO/Eina
    überlebt keinen fork, jedes Evas-Objekt im Kind ist ungültig und
    hinterlässt korrupte State im GUI.

12. Bei selbst gerenderten ARGB-Buffern als Evas-Image:
    `evas_object_image_fill_set` zwingend, sonst rendert nichts (Fill-Rect
    ist per Default 0x0). Und Evas-Buffer-Engine rendert nur in den
    Backing-Store; gevas braucht den
    `EVAS_CALLBACK_RENDER_FLUSH_POST`-Callback, der
    gtk_widget_queue_draw auslöst, sonst sieht man gerenderte Frames nie.

13. **Niemals Image-API auf Nicht-Image-Objekten**: `evas_object_image_
    fill_set/load_error_get/size_get` sind image-only. Live-Thumbnails
    (Emotion) und Edje-Gruppen sind Smart-Objects, das Tint im
    bg_trans-Hintergrund ein Rectangle. Aufruf auf denen = eina-safety-
    Spam bis SIGSEGV. Vor jedem Image-API-Aufruf auf fremde Objekte:
    `!strcmp(evas_object_type_get(o),"image")` prüfen (Muster jetzt in
    tt_traditional, bg_trans, bg_edje, ic_edje).

14. **Theme-Werte sind nicht vertrauenswürdig**: efm lieferte 2005
    `tooltip_font_size: 155555` (Vertipper für 15) — evas' Glyphen-
    Rasterizer crasht bei absurden Größen. tt_traditional klemmt
    Font-Größen auf 4..128 (Fallback 18). Bei neuen Themesin-
    Parametern: plausible Grenzen prüfen.

15. Engine-Plugins ia_*.c muessen mit ia_common.c gelinkt werden,
    ix_*.c mit ix_textpath.c (siehe engines/Makefile.am von 2005).
    Ohne die Common-Quellen fehlen Symbole und dlopen lehnt ab.

16. configure.sh muss erkannte Features auch nach config.mk exportieren
    (nicht nur nach config.h), sonst baut das Makefile sie nie.

17. libextractor: `EXTRACTOR_OPTION_IN_PROCESS` verwenden (Fork-Helper
    zerstören die X-Connection, Fatal IO error 11), und das
    gstreamer-Extraktor-Plugin per `EXTRACTOR_plugin_remove` entfernen
    (spinnt einen eigenen GLib-Mainloop, reentered GTK-Code, Crash).

18. **Kein `find -o` ohne Klammern**: Die Install-Härtung
    `find DIR -name '*.png' -o -name '*.jpg' -print -quit` löschte
    mangels Präzedenz alle PNG-only-Sets. Richtig:
    `find DIR \( -name '*.png' -o -name '*.jpg' \) -print -quit`.

19. **Fail-Einträge verfallen nicht von selbst**: `~/.thumbnails/fail/`
    blockt fehlgeschlagene Dateien still und dauerhaft (nur bei
    geänderter Quelldatei-mtime wird erneut versucht). Nach Crash-
    Sessions: `rm -rf ~/.thumbnails/fail` und
    `rm -rf ~/.evidence/cache` (Crash-Reste im Modell-Cache können beim
    nächsten Start SIGABRT geben).

20. **Zwischen Branches/PRs bauen prüfen!** Mehrere Fixes liefen auf
    separaten Branches — ein `make install` auf dem falschen Branch
    installiert alte, kaputte Plugins wieder und die Symptome sehen aus
    wie neue Bugs (Video-Thumbs "plötzlich wieder kaputt"). Vor Tests:
    `git branch --show-current` und alle relevanten PRs in main mergen.

## Monitor-Layer (inotify): Hinweise aus PR #11

- inotify liefert in `event->name` NUR den nackten Dateinamen. Die
  inotify-Route in `file_direct.c` muss ihn gegen das überwachte
  Verzeichnis (`m->have`) auflösen, bevor er an `file_monitor_event()`
  geht — die FAM-Route macht das äquivalent. Bei Events ohne `name`
  (IN_DELETE_SELF, MOVED_FROM auf dem Watch-Dir selbst) ist `m->have`
  selbst der Tag.
- `file_monitor_event()`: Bei NEW/CHG für ein NICHT im Modell vorhandenes
  Item MUSS `file_info_insert(NULL,tag)` verwendet werden (nicht
  `file_info_new()`) — nur so bekommt das Item einen Parent und die
  Views akzeptieren es (`icon_view.c` verwirft Events ohne Parent==cwd).
  Nur DEL nutzt ein Wegwerf-Item via `file_info_new()`, da
  `model_evt(MODEL_DEL)` es in jedem Fall zerstört und unref't.
- `model_evt(fi,MODEL_DEL,...)` unref't und zerstört das übergebene
  `file_info` selbst. Wer ein eigenes temporäres Item übergibt, darf
  es danach NICHT nochmal unref'en (war Double-Unref -> subzero
  refcount, UAF, SIGSEGV beim Fenster-Schließen).
- Rename ist im Modell ein DEL+NEW-Paar (alter Name raus, neuer rein).
  Wenn nach einem Rename das Icon verschwindet: NEW-Event erreicht das
  Modell nicht mit Parent — Punkt 2 oben prüfen.
- Kontrollpunkte im Log: INOTIFY-Events müssen VOLLE Pfade zeigen
  (`... for "/abs/pfad/datei"`), keine nackten Namen; keine Zeile
  "subzero refcount".

## Bekannt und harmlos

- Die Property `evas-engine` sagt noch `software_x11`, wird intern zu
  `buffer` gezwungen. Startlog ist still, Diagnose mit `--debug 3`.
- fishtank: `bg_trans` loggt pro Scroll "cannot create transparency
  pixmap, no valid wallpaper..." — informativ (Pseudo-Transparenz ohne
  festen Desktop-Wallpaper), kein Fehler.
- LCARS: fehlende `about.jpg` (Credits-Bild leer) und fehlende
  `LCARS_ul.png` im Theme — 2005er Auslieferungslücken, kein Crash.
- `--theme fonts` scheitert erwartbar (Daten-Ordner, kein Theme).
- Doppelte Thumbnail-Enqueues (Re-Visit-Retry + Scan) sind idempotent —
  potenzielles Dedup-Thema für später.
- Offene Performance-Themen (für später): Thumbnail-Worker ist
  single-threaded (100+ Videos = sequenziell), Evas-Damage wird pro
  Icon flächig addiert (O(n²) Renderarbeit bei großen Ordnern),
  linearer Icon-Lookup pro Event.
