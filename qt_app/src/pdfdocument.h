#ifndef PDFDOCUMENT_H
#define PDFDOCUMENT_H

#include <QObject>
#include <QImage>
#include <QSize>
#include <QRectF>
#include <QMutex>
#include <QThread>
#include <QFuture>
#include <QFutureWatcher>
#include <memory>

// Forward declare DLL handle types
struct PDF_Document;
struct PDF_Page;
struct PDF_ElementHandle;
struct PDF_ElementIteratorHandle;

// Compression structures (matching DLL API)
struct PDF_CompressOptions {
    int flags = 0;
    int image_quality = 90;
    int image_dpi_threshold = 300;
    int min_image_dpi = 150;
    int font_subset_threshold = 80;
    int remove_annotations = 0;
    int remove_forms = 0;
    int remove_bookmarks = 0;
    int remove_metadata = 0;
};

struct PDF_CompressStats {
    size_t original_size = 0;
    size_t compressed_size = 0;
    double compression_ratio = 0.0;
    int objects_removed = 0;
    int images_recompressed = 0;
    int fonts_subsets = 0;
};

// Element structures
enum class PdfElementType {
    Unknown = 0,
    Text = 1,
    Image = 2,
    Path = 3,
    Shading = 4,
    Form = 5,
    Group = 6,
    Reference = 7,
};

struct PdfTextAttributes {
    double font_size = 0;
    double char_spacing = 0;
    double word_spacing = 0;
    double horizontal_scaling = 0;
    double leading = 0;
    unsigned int font_flags = 0;
    unsigned int color_rgb = 0;
    unsigned int color_alpha = 0;
    char font_name[256] = {0};
    char font_family[128] = {0};
    int writing_mode = 0;
    double text_matrix[6] = {0};
};

struct PdfImageAttributes {
    int width = 0;
    int height = 0;
    int bits_per_component = 0;
    int color_space = 0;
    int filter = 0;
    size_t data_size = 0;
    double matrix[6] = {0};
    int has_mask = 0;
    int is_inline = 0;
};

struct PdfPathAttributes {
    int fill_color_rgb = 0;
    int fill_color_alpha = 0;
    int stroke_color_rgb = 0;
    int stroke_color_alpha = 0;
    double line_width = 0;
    int line_cap = 0;
    int line_join = 0;
    double miter_limit = 0;
    int fill_rule = 0;
    double dash_pattern[16] = {0};
    int dash_count = 0;
    double dash_phase = 0;
    double matrix[6] = {0};
};

struct PdfElementInfo {
    PdfElementType type = PdfElementType::Unknown;
    QRectF bounds;
    PdfTextAttributes text_attrs;
    PdfImageAttributes image_attrs;
    PdfPathAttributes path_attrs;
};

struct PdfFormFieldInfo {
    QString name;
    QString value;
    int field_type = 0;
    QRectF bounds;
    int flags = 0;
};

struct PdfAnnotInfo {
    int type = 0;
    QString contents;
    QRectF bounds;
    int flags = 0;
};

struct PdfBookmarkInfo {
    QString title;
    int page_index = -1;
    double dest_x = 0, dest_y = 0;
    double zoom = 1.0;
    int level = 0;
    int child_count = 0;
};

struct PdfDocumentStructure {
    int page_count = 0;
    int object_count = 0;
    int font_count = 0;
    int image_count = 0;
    int form_field_count = 0;
    int annotation_count = 0;
    int bookmark_count = 0;
    size_t file_size = 0;
    QString producer;
    QString creator;
    QString creation_date;
    QString mod_date;
};

class PdfDocument : public QObject {
    Q_OBJECT
public:
    explicit PdfDocument(QObject* parent = nullptr);
    ~PdfDocument();

    bool load(const QString& filePath, const QString& password = QString());
    void close();
    
    bool isLoaded() const { return m_docHandle != nullptr; }
    int pageCount() const { return m_pageCount; }
    QSizeF pageSize(int pageIndex) const;
    
    // Async rendering
    void requestRender(int pageIndex, const QSize& size, qreal rotation = 0);
    void cancelRender();
    
    // Text extraction
    QString getPageText(int pageIndex) const;
    QList<QRectF> searchText(int pageIndex, const QString& text, bool caseSensitive = false);
    
    // ============ New Compression API ============
    // Compression flags
    enum CompressFlag {
        CompressNone = 0,
        CompressFlate = 1 << 0,
        CompressObjectStreams = 1 << 1,
        CompressImages = 1 << 2,
        CompressFonts = 1 << 3,
        CompressRemoveUnused = 1 << 4,
        CompressLinearize = 1 << 5,
        CompressLossless = CompressFlate | CompressObjectStreams | CompressRemoveUnused,
        CompressDefault = CompressLossless | CompressLinearize,
    };
    Q_DECLARE_FLAGS(CompressFlags, CompressFlag)
    
    // Optimize/compress document (async)
    void optimizeDocument(const CompressFlags& flags, const QString& outputPath);
    void saveWithCompression(const QString& filePath, const CompressFlags& flags);
    
    // Get last compression stats
    PDF_CompressStats getLastCompressStats() const;
    
    // ============ New Element Extraction API ============
    // Count elements on page
    int countPageElements(int pageIndex) const;
    
    // Get element info by index
    PdfElementInfo getPageElement(int pageIndex, int elementIndex) const;
    
    // Find elements by type
    QList<PdfElementInfo> findElementsByType(int pageIndex, PdfElementType type) const;
    
    // Extract text from specific element
    QString getElementText(int pageIndex, int elementIndex) const;
    
    // Extract image data from element
    QByteArray getElementImageData(int pageIndex, int elementIndex) const;
    
    // Extract path data from element
    QByteArray getElementPathData(int pageIndex, int elementIndex) const;
    
    // Extract form fields
    QList<PdfFormFieldInfo> extractFormFields() const;
    
    // Extract annotations
    QList<PdfAnnotInfo> extractAnnotations(int pageIndex) const;
    
    // Extract bookmarks/outline
    QList<PdfBookmarkInfo> extractBookmarks() const;
    
    // Get document structure info
    PdfDocumentStructure getDocumentStructure() const;
    
    // Extended text search with element context
    struct TextMatchEx {
        QRectF bounds;
        int char_index = -1;
        int element_index = -1;
    };
    QList<TextMatchEx> searchTextEx(int pageIndex, const QString& text, bool caseSensitive = false) const;

signals:
    void renderFinished(int pageIndex, QImage image, bool success);
    void loadFinished(bool success, const QString& errorMessage);
    void pageCountChanged(int count);
    void errorOccurred(const QString& message);
    
    // New signals for compression
    void optimizeFinished(bool success, const QString& outputPath, const QString& errorMessage);
    void saveCompressedFinished(bool success, const QString& filePath, const QString& errorMessage);

private:
    struct Private;
    std::unique_ptr<Private> d;
    
    // DLL handles
    PDF_Document* m_docHandle = nullptr;
    int m_pageCount = 0;
    mutable QMutex m_mutex;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(PdfDocument::CompressFlags)

#endif // PDFDOCUMENT_H