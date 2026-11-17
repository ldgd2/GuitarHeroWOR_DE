// Modos de captura. "wire" es el nombre en el protocolo con el editor y con el codigo nativo.
enum CaptureMode {
  body('body', 'Cuerpo', 'Cuerpo entero, sin cara. Ponte a 2-3 m con todo el cuerpo en la imagen (camara trasera en un tripode da mas precision).', false),
  head('head', 'Cabeza y rostro', 'Giro de la cabeza y gestos. Telefono a la altura de la cara, a 40-60 cm.', true),
  face('face', 'Rostro', 'Solo gestos, con mas detalle: la cara debe llenar la imagen (25-35 cm). La cabeza la pone la animacion del juego.', true);

  const CaptureMode(this.wire, this.title, this.hint, this.frontDefault);
  final String wire, title, hint;
  final bool frontDefault;
  bool get isFace => this != CaptureMode.body;
}
