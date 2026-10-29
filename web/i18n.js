// A.S.T.R.O AI interface languages (1.7): English, French, Arabic (right to left).
// A dictionary translator: exact UI strings (text, placeholder, title, aria-label) are swapped in place, and a
// MutationObserver catches UI that is drawn later. AI answers, your messages, code, editors and terminals are
// never touched. The English text is kept, so switching back restores it exactly.
(() => {
  'use strict';
  const KEY = 'astro.uiLang';
  const LANGS = { en: 'English', fr: 'Français', ar: 'العربية' };

  // English -> [French, Arabic]
  const D = {
    'New chat': ['Nouvelle discussion', 'محادثة جديدة'],
    'Chat': ['Discussion', 'محادثة'],
    'Workspace': ['Espace de travail', 'مساحة العمل'],
    'Agent': ['Agent', 'الوكيل'],
    'New': ['Nouveau', 'جديد'],
    'Notes': ['Notes', 'الملاحظات'],
    'Tasks': ['Tâches', 'المهام'],
    'Reader': ['Lecteur', 'القارئ'],
    'Model Lab': ['Labo des modèles', 'مختبر النماذج'],
    'Knowledge': ['Connaissances', 'المعرفة'],
    'Calculator': ['Calculatrice', 'الآلة الحاسبة'],
    'Recent': ['Récents', 'الأخيرة'],
    'Your chats will appear here.': ['Vos discussions apparaîtront ici.', 'ستظهر محادثاتك هنا.'],
    'Connecting…': ['Connexion…', 'جارٍ الاتصال…'],
    'Settings & help': ['Paramètres et aide', 'الإعدادات والمساعدة'],
    'Auto': ['Auto', 'تلقائي'],
    'Search or jump to…': ['Rechercher ou aller à…', 'ابحث أو انتقل إلى…'],
    'Live': ['Direct', 'مباشر'],
    'Hello there': ['Bonjour', 'مرحبًا'],
    'How can I help you today?': ['Comment puis-je vous aider aujourd’hui ?', 'كيف يمكنني مساعدتك اليوم؟'],
    'General': ['Général', 'عام'],
    'Compare': ['Comparer', 'مقارنة'],
    'Web': ['Web', 'الويب'],
    'Explain a concept': ['Expliquer une notion', 'اشرح مفهومًا'],
    'Write a draft': ['Rédiger un brouillon', 'اكتب مسودة'],
    'Plan something': ['Planifier quelque chose', 'خطّط لشيء'],
    'Write code': ['Écrire du code', 'اكتب شيفرة'],
    'Summarize a file': ['Résumer un fichier', 'لخّص ملفًا'],
    'Brainstorm': ['Remue-méninges', 'عصف ذهني'],
    'Give it a job, it works on its own': ['Donnez-lui une tâche, il travaille seul', 'أعطه مهمة وسيعمل وحده'],
    'Code, run and preview projects': ['Coder, exécuter et prévisualiser des projets', 'برمج وشغّل واعرض المشاريع'],
    'Write, organise, ask about them': ['Écrire, organiser, poser des questions', 'اكتب ونظّم واسأل عنها'],
    'Read any page or file with AI': ['Lire n’importe quelle page ou fichier avec l’IA', 'اقرأ أي صفحة أو ملف بالذكاء الاصطناعي'],
    'A.S.T.R.O AI runs on free AI models and can make mistakes. Double-check important info.': ['A.S.T.R.O AI utilise des modèles d’IA gratuits et peut se tromper. Vérifiez les informations importantes.', 'يعمل A.S.T.R.O AI بنماذج ذكاء اصطناعي مجانية وقد يخطئ. تحقّق من المعلومات المهمة.'],
    'Capture ideas. Saved only in this browser.': ['Notez vos idées. Enregistrées uniquement dans ce navigateur.', 'دوّن أفكارك. تُحفظ في هذا المتصفح فقط.'],
    'New note': ['Nouvelle note', 'ملاحظة جديدة'],
    'Ask AI': ['Demander à l’IA', 'اسأل الذكاء الاصطناعي'],
    'Export': ['Exporter', 'تصدير'],
    'Delete': ['Supprimer', 'حذف'],
    'Notes are saved only in this browser.': ['Les notes sont enregistrées uniquement dans ce navigateur.', 'تُحفظ الملاحظات في هذا المتصفح فقط.'],
    'A private list for what\'s next.': ['Une liste privée pour la suite.', 'قائمة خاصة لما هو قادم.'],
    'Open': ['Ouvrir', 'فتح'],
    'Due today': ['Pour aujourd’hui', 'مستحقة اليوم'],
    'Completed': ['Terminées', 'مكتملة'],
    'Normal': ['Normale', 'عادية'],
    'High': ['Haute', 'عالية'],
    'Low': ['Basse', 'منخفضة'],
    'Add': ['Ajouter', 'إضافة'],
    'All': ['Tout', 'الكل'],
    'navigate': ['naviguer', 'تنقّل'],
    'open': ['ouvrir', 'فتح'],
    'close': ['fermer', 'إغلاق'],
    'Drop files to add them to Knowledge': ['Déposez des fichiers pour les ajouter aux connaissances', 'أفلت الملفات لإضافتها إلى المعرفة'],
    'One Brain': ['Cerveau unique', 'العقل الواحد'],
    'Voice interrupt': ['Interrompre à la voix', 'المقاطعة بالصوت'],
    'Starting…': ['Démarrage…', 'جارٍ البدء…'],
    'Main menu': ['Menu principal', 'القائمة الرئيسية'],
    'Chat history': ['Historique des discussions', 'سجل المحادثات'],
    'Hide chat history (Ctrl+B)': ['Masquer l’historique (Ctrl+B)', 'إخفاء سجل المحادثات (Ctrl+B)'],
    'Search': ['Rechercher', 'بحث'],
    'Search everything (Ctrl+K)': ['Tout rechercher (Ctrl+K)', 'ابحث في كل شيء (Ctrl+K)'],
    'New chat (Ctrl+Shift+O)': ['Nouvelle discussion (Ctrl+Shift+O)', 'محادثة جديدة (Ctrl+Shift+O)'],
    'Workspace: files, code, run and preview': ['Espace de travail : fichiers, code, exécution et aperçu', 'مساحة العمل: ملفات وشيفرة وتشغيل ومعاينة'],
    'Agent: works on its own with tools until the job is done': ['Agent : travaille seul avec des outils jusqu’à la fin de la tâche', 'الوكيل: يعمل وحده بالأدوات حتى ينتهي العمل'],
    'Reader: read any page or file': ['Lecteur : lire n’importe quelle page ou fichier', 'القارئ: اقرأ أي صفحة أو ملف'],
    'Model Lab: test models': ['Labo des modèles : tester les modèles', 'مختبر النماذج: اختبر النماذج'],
    'Knowledge files': ['Fichiers de connaissances', 'ملفات المعرفة'],
    'Delete all chats': ['Supprimer toutes les discussions', 'حذف كل المحادثات'],
    'Search chats': ['Rechercher dans les discussions', 'ابحث في المحادثات'],
    'Brain: free model platforms': ['Cerveau : plateformes de modèles gratuits', 'العقل: منصات النماذج المجانية'],
    'Open menu': ['Ouvrir le menu', 'فتح القائمة'],
    'Choose a model': ['Choisir un modèle', 'اختر نموذجًا'],
    'Search chats, notes, tasks and commands': ['Rechercher discussions, notes, tâches et commandes', 'ابحث في المحادثات والملاحظات والمهام والأوامر'],
    'Start a live voice conversation': ['Démarrer une conversation vocale', 'ابدأ محادثة صوتية مباشرة'],
    'Export chat as Markdown': ['Exporter la discussion en Markdown', 'تصدير المحادثة بصيغة Markdown'],
    'Export chat': ['Exporter la discussion', 'تصدير المحادثة'],
    'Switch theme': ['Changer de thème', 'تبديل السمة'],
    'Settings': ['Paramètres', 'الإعدادات'],
    'Conversation': ['Conversation', 'المحادثة'],
    'Scroll to latest': ['Aller au plus récent', 'انتقل إلى الأحدث'],
    'Ask A.S.T.R.O AI': ['Demander à A.S.T.R.O AI', 'اسأل A.S.T.R.O AI'],
    'Message A.S.T.R.O AI': ['Écrire à A.S.T.R.O AI', 'راسل A.S.T.R.O AI'],
    'Add files and more': ['Ajouter des fichiers et plus', 'أضف ملفات والمزيد'],
    'Work mode': ['Mode de travail', 'وضع العمل'],
    'Compare two models side by side': ['Comparer deux modèles côte à côte', 'قارن نموذجين جنبًا إلى جنب'],
    'Search the web for this message (answers cite sources)': ['Rechercher sur le web pour ce message (réponses avec sources)', 'ابحث في الويب لهذه الرسالة (الإجابات تذكر المصادر)'],
    'Response language': ['Langue des réponses', 'لغة الإجابات'],
    'Dictate': ['Dicter', 'إملاء'],
    'Dictate (browser speech)': ['Dicter (reconnaissance du navigateur)', 'إملاء (التعرّف الصوتي في المتصفح)'],
    'Start Live voice conversation': ['Démarrer la conversation vocale en direct', 'ابدأ المحادثة الصوتية المباشرة'],
    'Live: talk with the brain by voice': ['Direct : parler au cerveau à la voix', 'مباشر: تحدّث مع العقل بالصوت'],
    'Send message': ['Envoyer le message', 'إرسال الرسالة'],
    'Open a space': ['Ouvrir un espace', 'افتح مساحة'],
    'Search notes': ['Rechercher dans les notes', 'ابحث في الملاحظات'],
    'Untitled note': ['Note sans titre', 'ملاحظة بلا عنوان'],
    'Note title': ['Titre de la note', 'عنوان الملاحظة'],
    'Ask A.S.T.R.O AI about this note': ['Interroger A.S.T.R.O AI sur cette note', 'اسأل A.S.T.R.O AI عن هذه الملاحظة'],
    'Start writing…': ['Commencez à écrire…', 'ابدأ الكتابة…'],
    'Note content': ['Contenu de la note', 'محتوى الملاحظة'],
    'Add a task…': ['Ajouter une tâche…', 'أضف مهمة…'],
    'New task': ['Nouvelle tâche', 'مهمة جديدة'],
    'Due date': ['Échéance', 'تاريخ الاستحقاق'],
    'Priority': ['Priorité', 'الأولوية'],
    'Search tasks': ['Rechercher dans les tâches', 'ابحث في المهام'],
    'Command palette': ['Palette de commandes', 'لوحة الأوامر'],
    'Search chats, models, actions…': ['Rechercher discussions, modèles, actions…', 'ابحث في المحادثات والنماذج والإجراءات…'],
    'Close': ['Fermer', 'إغلاق'],
    'Live voice conversation': ['Conversation vocale en direct', 'محادثة صوتية مباشرة'],
    'Speech language': ['Langue de la voix', 'لغة الكلام'],
    'Close Live': ['Fermer le direct', 'إغلاق المباشر'],
    'Type instead of speaking…': ['Écrivez au lieu de parler…', 'اكتب بدل التحدّث…'],
    'Type a message': ['Écrivez un message', 'اكتب رسالة'],
    'Send': ['Envoyer', 'إرسال'],
    'Mute microphone': ['Couper le micro', 'كتم الميكروفون'],
    'End Live': ['Terminer le direct', 'إنهاء المباشر'],
    // Settings
    'Appearance': ['Apparence', 'المظهر'],
    'Brain': ['Cerveau', 'العقل'],
    'Providers & keys': ['Fournisseurs et clés', 'المزوّدون والمفاتيح'],
    'Account & sync': ['Compte et synchronisation', 'الحساب والمزامنة'],
    'Memory': ['Mémoire', 'الذاكرة'],
    'Data & privacy': ['Données et confidentialité', 'البيانات والخصوصية'],
    'Shortcuts': ['Raccourcis', 'الاختصارات'],
    'About': ['À propos', 'حول'],
    'Voice': ['Voix', 'الصوت'],
    'Speaking speed': ['Vitesse de lecture', 'سرعة الكلام'],
    'Read every reply aloud': ['Lire chaque réponse à voix haute', 'اقرأ كل إجابة بصوت عالٍ'],
    'Mode': ['Mode', 'الوضع'],
    'Your name': ['Votre nom', 'اسمك'],
    'Theme': ['Thème', 'السمة'],
    'Text size': ['Taille du texte', 'حجم النص'],
    'Enter sends message': ['Entrée envoie le message', 'زر الإدخال يرسل الرسالة'],
    'Interface language': ['Langue de l’interface', 'لغة الواجهة'],
    'Updates': ['Mises à jour', 'التحديثات'],
    'Report a problem': ['Signaler un problème', 'الإبلاغ عن مشكلة'],
    'Report…': ['Signaler…', 'إبلاغ…'],
    'Check now': ['Vérifier', 'تحقّق الآن'],
    'Minds per answer': ['Esprits par réponse', 'العقول لكل إجابة'],
    'Wait for every mind': ['Attendre chaque esprit', 'انتظر كل العقول'],
    'Show the fastest answer first': ['Afficher d’abord la réponse la plus rapide', 'اعرض أسرع إجابة أولًا'],
    'Patience': ['Patience', 'الصبر'],
    'Check code in the background': ['Vérifier le code en arrière-plan', 'افحص الشيفرة في الخلفية'],
    'A.S.T.R.O AI actions': ['Actions d’A.S.T.R.O AI', 'إجراءات A.S.T.R.O AI'],
    'Export everything': ['Tout exporter', 'تصدير كل شيء'],
    'Import': ['Importer', 'استيراد'],
    'Clear all local data': ['Effacer toutes les données locales', 'مسح كل البيانات المحلية'],
    'Allow providers that may train on prompts': ['Autoriser les fournisseurs qui peuvent s’entraîner sur les messages', 'السماح بالمزوّدين الذين قد يتدرّبون على الرسائل'],
    'Sync across devices': ['Synchroniser entre appareils', 'المزامنة بين الأجهزة'],
    'Sync now': ['Synchroniser', 'زامن الآن'],
    'Change password': ['Changer le mot de passe', 'تغيير كلمة المرور'],
    'Sign out': ['Se déconnecter', 'تسجيل الخروج'],
    'Install A.S.T.R.O AI': ['Installer A.S.T.R.O AI', 'ثبّت A.S.T.R.O AI'],
    'Get more free models': ['Obtenir plus de modèles gratuits', 'احصل على نماذج مجانية أكثر'],
    'A.S.T.R.O API key': ['Clé API A.S.T.R.O', 'مفتاح A.S.T.R.O API'],
    'Web search keys (optional)': ['Clés de recherche web (facultatif)', 'مفاتيح البحث في الويب (اختياري)'],
    'Cancel': ['Annuler', 'إلغاء'],
    'Update': ['Mettre à jour', 'تحديث'],
    'Update now': ['Mettre à jour', 'حدّث الآن'],
    'Download update': ['Télécharger la mise à jour', 'نزّل التحديث'],
    'Get it': ['L’obtenir', 'احصل عليه'],
    'What\'s new': ['Nouveautés', 'ما الجديد'],
    'Later': ['Plus tard', 'لاحقًا'],
    'Add a free key': ['Ajouter une clé gratuite', 'أضف مفتاحًا مجانيًا'],
    'Try again': ['Réessayer', 'حاول مجددًا'],
    'Use this answer': ['Utiliser cette réponse', 'استخدم هذه الإجابة'],
    'Skip': ['Passer', 'تخطٍّ'],
    'Test voice': ['Tester la voix', 'اختبر الصوت'],
    'Start Live': ['Démarrer le direct', 'ابدأ المباشر'],
    'Get themes': ['Obtenir des thèmes', 'احصل على سمات'],
    'Test the brain': ['Tester le cerveau', 'اختبر العقل'],
    'Refresh status': ['Actualiser l’état', 'حدّث الحالة'],
    'Export JSON': ['Exporter en JSON', 'تصدير JSON'],
    'Delete chats': ['Supprimer les discussions', 'حذف المحادثات'],
    'Clear data': ['Effacer les données', 'مسح البيانات'],
    'Done': ['Terminé', 'تم'],
    'Undo': ['Annuler', 'تراجع'],
    'Run again': ['Relancer', 'شغّل مجددًا'],
    'Open in a new tab': ['Ouvrir dans un nouvel onglet', 'افتح في علامة تبويب جديدة'],
    'Save as HTML': ['Enregistrer en HTML', 'احفظ بصيغة HTML'],
    'Save': ['Enregistrer', 'حفظ'],
    'Default': ['Par défaut', 'افتراضي'],
    'Remove': ['Retirer', 'إزالة'],
    'Test': ['Tester', 'اختبار'],
    'Test all connected': ['Tester tous les connectés', 'اختبر كل المتصل'],
    'Refresh': ['Actualiser', 'تحديث'],
    'Download': ['Télécharger', 'تنزيل'],
    'Change': ['Modifier', 'تغيير'],
    'Install': ['Installer', 'تثبيت'],
    'Read': ['Lire', 'اقرأ'],
    'Open a file': ['Ouvrir un fichier', 'افتح ملفًا'],
    'Summarize': ['Résumer', 'لخّص'],
    'Ask about it': ['Poser une question', 'اسأل عنه'],
    'Save as note': ['Enregistrer comme note', 'احفظ كملاحظة'],
    'Read aloud': ['Lire à voix haute', 'اقرأ بصوت عالٍ'],
    'Add to knowledge': ['Ajouter aux connaissances', 'أضف إلى المعرفة'],
    'Copy text': ['Copier le texte', 'انسخ النص'],
    'Copy': ['Copier', 'نسخ'],
    'Copied to clipboard': ['Copié dans le presse-papiers', 'تم النسخ إلى الحافظة'],
    'Retry': ['Réessayer', 'أعد المحاولة'],
    'Edit': ['Modifier', 'تعديل'],
    'Stop': ['Arrêter', 'إيقاف'],
    'Allow': ['Autoriser', 'سماح'],
    'Allow all in this run': ['Tout autoriser pour cette exécution', 'السماح بالكل في هذا التشغيل'],
    'Deny': ['Refuser', 'رفض'],
    'Plan': ['Planifier', 'تخطيط'],
    'Ask': ['Demander', 'سؤال'],
    'Files': ['Fichiers', 'الملفات'],
    'Terminal': ['Terminal', 'الطرفية'],
    'Preview': ['Aperçu', 'معاينة'],
    'Run': ['Exécuter', 'تشغيل'],
    'Extensions': ['Extensions', 'الإضافات'],
    'Plugins': ['Plugins', 'المكوّنات الإضافية'],
    'Themes': ['Thèmes', 'السمات'],
    'Snippets': ['Extraits', 'المقتطفات'],
    'Skills': ['Compétences', 'المهارات'],
    'Problems': ['Problèmes', 'المشكلات'],
    'Source control': ['Contrôle de source', 'التحكم في المصدر'],
    'New file': ['Nouveau fichier', 'ملف جديد'],
    'New folder': ['Nouveau dossier', 'مجلد جديد'],
    'Rename': ['Renommer', 'إعادة تسمية'],
    'Something went wrong in A.S.T.R.O AI.': ['Un problème est survenu dans A.S.T.R.O AI.', 'حدث خطأ ما في A.S.T.R.O AI.'],
    'Report it': ['Le signaler', 'أبلغ عنه'],
    'Needs attention': ['Demande votre attention', 'يحتاج إلى انتباه'],
    'Install cancelled.': ['Installation annulée.', 'أُلغي التثبيت.'],
    'Text and settings only': ['Texte et réglages seulement', 'نص وإعدادات فقط'],
    'Runs commands automatically': ['Exécute des commandes automatiquement', 'ينفّذ أوامر تلقائيًا'],
    'Starts programs (MCP servers)': ['Lance des programmes (serveurs MCP)', 'يشغّل برامج (خوادم MCP)'],
    'Paste the key first': ['Collez d’abord la clé', 'الصق المفتاح أولًا'],
    'Questions and ideas': ['Questions et idées', 'أسئلة وأفكار'],
    'Ask the community, suggest features and share what you built.': ['Posez vos questions, proposez des idées et partagez vos créations.', 'اسأل المجتمع واقترح ميزات وشارك ما صنعته.'],
    'Discussions ↗': ['Discussions ↗', 'النقاشات ↗'],
    'Good morning': ['Bonjour', 'صباح الخير'],
    'Good afternoon': ['Bon après-midi', 'طاب يومك'],
    'Good evening': ['Bonsoir', 'مساء الخير'],
    'Good night': ['Bonne nuit', 'ليلة سعيدة'],
    'Ask two models at once': ['Interroger deux modèles à la fois', 'اسأل نموذجين معًا'],
    'Installed with Scoop: run': ['Installé avec Scoop : lancez', 'مثبّت عبر Scoop: شغّل'],
    'Installed with Homebrew: run': ['Installé avec Homebrew : lancez', 'مثبّت عبر Homebrew: شغّل'],
    'Terminal install: run': ['Installation terminal : lancez', 'تثبيت الطرفية: شغّل'],
  };
  // Dynamic text: [regex, French, Arabic]
  const P = [
    [/^Using (\d+) models · (\d+) platforms$/, '$1 modèles utilisés · $2 plateformes', 'يستخدم $1 نموذجًا · $2 منصة'],
    [/^Brain · (\d+)\/(\d+) models · (\d+) platforms$/, 'Cerveau · $1/$2 modèles · $3 plateformes', 'العقل · $1/$2 نموذجًا · $3 منصة'],
    [/^A\.S\.T\.R\.O AI ([\d.]+) is available$/, 'A.S.T.R.O AI $1 est disponible', 'الإصدار $1 من A.S.T.R.O AI متاح'],
    [/^You have ([\d.]+)$/, 'Vous avez la $1', 'لديك $1'],
    [/^You have ([\d.]+) · released (.+)$/, 'Vous avez la $1 · publiée le $2', 'لديك $1 · صدر في $2'],
    [/^One Brain · all (\d+) minds together$/, 'Un seul cerveau · les $1 esprits ensemble', 'عقل واحد · جميع العقول الـ$1 معًا'],
    [/^One Brain · (\d+) minds together$/, 'Un seul cerveau · $1 esprits ensemble', 'عقل واحد · $1 عقلًا معًا'],
    [/^Ask A\.S\.T\.R\.O AI · (.+) mode$/, 'Demander à A.S.T.R.O AI · mode $1', 'اسأل A.S.T.R.O AI · وضع $1'],
    [/^Quick answer from $/, 'Réponse rapide de ', 'إجابة سريعة من '],
    [/^ · refining with (\d+) models…$/, ' · affinage avec $1 modèles…', ' · يجري التحسين بـ $1 نموذجًا…'],
    [/^ · merging (\d+) drafts…$/, ' · fusion de $1 brouillons…', ' · دمج $1 مسودة…'],
    [/^ · reviewers are checking it…$/, ' · les relecteurs vérifient…', ' · المراجعون يتحققون…'],
    [/^ · writing the team answer…$/, ' · rédaction de la réponse de l’équipe…', ' · كتابة إجابة الفريق…'],
    [/^Install (.+)\?$/, 'Installer $1 ?', 'تثبيت $1؟'],
    [/^(\d+) models ready$/, '$1 modèles prêts', '$1 نموذجًا جاهزًا'],
  ];
  const SKIP = '.md, pre, code, kbd, samp, textarea, [contenteditable], .cm-editor, .xterm, .ws-editor, .ws-term, .term, .turn-user, .chat-list, .d-content, [data-no-i18n], script, style, svg';
  const ATTRS = ['placeholder', 'title', 'aria-label'];
  const textOrig = new WeakMap();   // Text node -> { en, shown }
  const attrOrig = new WeakMap();   // Element -> { attr: { en, shown } }
  let lang = 'en'; let obs = null;
  const idx = () => (lang === 'fr' ? 0 : 1);
  const norm = (s) => s.replace(/\s+/g, ' ').trim();

  function tr(en) {
    if (lang === 'en' || !en) return null;
    const k = norm(en);
    if (!k || !/[A-Za-z]/.test(k)) return null;
    const hit = D[k]; if (hit) return hit[idx()];
    for (const [re, fr, ar] of P) if (re.test(en)) return en.replace(re, lang === 'fr' ? fr : ar);
    return null;
  }
  const ATTR_ONLY = 'textarea, [contenteditable]';   // translate the placeholder, never the contents
  const skipped = (el) => !el || (el.closest && el.closest(SKIP));
  function doText(node) {
    const rec = textOrig.get(node);
    const en = rec && node.data === rec.shown ? rec.en : node.data;   // the app may have replaced the text since
    if (lang === 'en') { if (rec && node.data === rec.shown) node.data = rec.en; textOrig.delete(node); return; }
    const t = tr(en);
    if (t == null) { textOrig.delete(node); return; }
    const lead = en.match(/^\s*/)[0]; const tail = en.match(/\s*$/)[0];
    const shown = /^\s| $/.test(t) ? t : lead + t + tail;
    textOrig.set(node, { en, shown });
    if (node.data !== shown) node.data = shown;
  }
  function doAttrs(el) {
    let rec = attrOrig.get(el);
    for (const a of ATTRS) {
      if (!el.hasAttribute(a)) continue;
      const cur = el.getAttribute(a);
      const r = rec && rec[a];
      const en = r && cur === r.shown ? r.en : cur;
      if (lang === 'en') { if (r && cur === r.shown) el.setAttribute(a, r.en); continue; }
      const t = tr(en);
      if (t == null) continue;
      if (!rec) { rec = {}; attrOrig.set(el, rec); }
      rec[a] = { en, shown: t };
      if (cur !== t) el.setAttribute(a, t);
    }
  }
  function walk(root) {
    if (!root) return;
    if (root.nodeType === 3) { if (!skipped(root.parentElement)) doText(root); return; }
    if (root.nodeType !== 1) return;
    if (skipped(root)) { if (root.matches(ATTR_ONLY) && !(root.parentElement && skipped(root.parentElement))) doAttrs(root); return; }
    doAttrs(root);
    const w = document.createTreeWalker(root, NodeFilter.SHOW_ELEMENT | NodeFilter.SHOW_TEXT, {
      acceptNode: (n) => {
        if (n.nodeType !== 1 || !n.matches(SKIP)) return NodeFilter.FILTER_ACCEPT;
        if (n.matches(ATTR_ONLY)) doAttrs(n);
        return NodeFilter.FILTER_REJECT;
      },
    });
    for (let n = w.nextNode(); n; n = w.nextNode()) { if (n.nodeType === 3) doText(n); else doAttrs(n); }
  }
  let pending = new Set(); let queued = false;
  function flush() {
    queued = false;
    if (obs) obs.disconnect();
    const list = [...pending]; pending = new Set();
    for (const n of list) if (n.isConnected) walk(n);
    if (lang !== 'en') observe();
  }
  function observe() {
    if (!obs) obs = new MutationObserver((muts) => {
      for (const m of muts) {
        if (m.type === 'childList') m.addedNodes.forEach((n) => pending.add(n));
        else pending.add(m.target);
      }
      if (!queued && pending.size) { queued = true; queueMicrotask(flush); }
    });
    obs.observe(document.body, { subtree: true, childList: true, characterData: true, attributes: true, attributeFilter: ATTRS });
  }
  function apply(l) {
    lang = LANGS[l] ? l : 'en';
    document.documentElement.lang = lang;
    document.documentElement.dir = lang === 'ar' ? 'rtl' : 'ltr';
    if (obs) obs.disconnect();
    walk(document.body);
    if (lang !== 'en') observe();
  }
  function initial() {
    const saved = localStorage.getItem(KEY);
    if (saved && LANGS[saved]) return saved;
    const nav = String(navigator.language || 'en').slice(0, 2).toLowerCase();
    return LANGS[nav] ? nav : 'en';
  }
  window.AstroI18n = {
    LANGS,
    get: () => lang,
    set: (l) => { localStorage.setItem(KEY, l); apply(l); },
    t: (en) => tr(en) ?? en,
    size: () => Object.keys(D).length,
  };
  const start = () => apply(initial());
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start); else start();
})();
