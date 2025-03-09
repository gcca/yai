# yAI Webapp

Una aplicación web basada en Django.

## Características
- Procesamiento de texto rápido vía C++.
- Interfaz con endpoints Django.
- Preparada para nube (AWS) y contenedores (Docker).

## Instalación
1. Clona el repositorio: `git clone <url>`
2. Instala dependencias: `pip install -r requirements.txt`
3. Ejecuta: `cd yai && python manage.py runserver`

## Despliegue
Configurado para AWS CodeBuild con `buildspec.yml` o Docker:
```dockerfile
FROM python:3.12-slim
COPY . /app
RUN pip install -r requirements.txt && python setup.py build_ext --inplace
CMD ["python", "yai/manage.py", "runserver", "0.0.0.0:8000"]
```

## Contribuir
¡Aporta clonando, creando ramas y enviando PRs!

## Licencia
Ver archivo de licencia en el repositorio.
