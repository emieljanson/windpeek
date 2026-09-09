import { siteVariant } from '../src/marketing/siteVariant.js'

export function swellPageHtml(html) {
  const variant = siteVariant({ search: '?site=swell' })
  return html
    .replace('<head>', '<head>\n    <base href="../" />')
    .replace(/<title>[^<]*<\/title>/, '<title>Windpeek — Always-on swell forecast</title>')
    .replace(/(<meta (?:name="description"|property="og:description") content=")[^"]*/g, `$1${variant.description}`)
    .replace(/(<meta property="og:title" content=")[^"]*/, `$1${variant.title}`)
    .replace(/(<meta property="og:url" content=")[^"]*/, '$1https://windpeek.com/swell/')
    .replace(/(<link rel="canonical" href=")[^"]*/, '$1https://windpeek.com/swell/')
    .replace('windpeek-social-v17.jpg', 'windpeek-social-swell-v1.jpg')
    .replace('showing a five-day wind forecast on a reTerminal', 'showing a five-day swell forecast on a reTerminal')
}

export function swellPagePlugin() {
  return {
    name: 'swell-share-page',
    enforce: 'post',
    generateBundle(_, bundle) {
      const index = bundle['index.html']
      if (!index) throw new Error('Missing index.html for swell share page')
      this.emitFile({ type: 'asset', fileName: 'swell/index.html', source: swellPageHtml(String(index.source)) })
    },
  }
}
